/**
 * @file TcpSink.cpp
 * @brief MTcpSink 实现 —— 把日志按 JSON Lines 经 TCP 推给远端收集器,带重连退避。
 */
#include "Common/Runtime/Log/TcpSink.h"
#include "Common/Runtime/Json.h"
#include "Common/Runtime/Log/LogMetrics.h"
#include "Common/Runtime/Log/LogRegistry.h"
#include "Common/Runtime/Log/LogStringTable.h"

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {
    long long NowSteadyMs() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    void CopyMessage(const SLogRecord& R, char* Out, size_t OutSize) {
        const size_t Max = sizeof(R.Payload.Inline.Data);
        size_t       Len = 0;
        while (Len < Max && R.Payload.Inline.Data[Len] != '\0') {
            ++Len;
        }
        if (Len >= OutSize)
            Len = OutSize - 1;
        std::memcpy(Out, R.Payload.Inline.Data, Len);
        Out[Len] = '\0';
    }

    const char* ResolveCategoryName(uint16 Id) {
        const SLogCategory* Cat = MLogRegistry::Get().GetById(Id);
        return (Cat && Cat->Name) ? Cat->Name : "?";
    }

    const char* ResolveInterned(uint32 Id) {
        const char* S = MLogStringTable::Get(Id);
        return (S && S[0]) ? S : "";
    }

    /**
     * @brief 把一条记录组建成单个 JSON Line 写入 Out。
     *
     * @return 写入字节数(不含结尾 '\n');Out 容不下时返回 0。
     */
    size_t BuildJsonLine(const SLogRecord& R, char* Out, size_t OutSize) {
        char Message[64];
        CopyMessage(R, Message, sizeof(Message));
        const char*     CatName = ResolveCategoryName(R.CategoryId);
        const char*     FileStr = ResolveInterned(R.FileStringId);
        const char*     FuncStr = ResolveInterned(R.FuncStringId);
        const ELogLevel Lvl     = static_cast<ELogLevel>(R.Level);

        MJsonWriter W = MJsonWriter::Object();
        W.Key("ts");
        W.Value(R.TimestampNs);
        W.Key("level");
        W.Value(MString(LogLevelToString(Lvl)));
        W.Key("category");
        W.Value(MString(CatName));
        W.Key("thread");
        W.Value(static_cast<uint64>(R.ThreadId));
        W.Key("file");
        W.Value(MString(FileStr));
        W.Key("line");
        W.Value(static_cast<uint64>(R.Line));
        W.Key("func");
        W.Value(MString(FuncStr));
        W.Key("msg");
        W.Value(MString(Message));
        W.EndObject();
        const MString& Line = W.ToString();

        const size_t Want = Line.size() + 1;
        if (Want > OutSize)
            return 0;
        std::memcpy(Out, Line.c_str(), Line.size());
        Out[Line.size()] = '\n';
        return Want;
    }

    bool ResolveTarget(const MString& Target, sockaddr_in& OutAddr) {
        const size_t Colon = Target.rfind(':');
        if (Colon == MString::npos)
            return false;
        const MString Host = Target.substr(0, Colon);
        const MString Port = Target.substr(Colon + 1);
        std::memset(&OutAddr, 0, sizeof(OutAddr));
        OutAddr.sin_family = AF_INET;
        if (::inet_pton(AF_INET, Host.c_str(), &OutAddr.sin_addr) != 1)
            return false;
        const int Pn = std::atoi(Port.c_str());
        if (Pn <= 0 || Pn > 65535)
            return false;
        OutAddr.sin_port = htons(static_cast<uint16>(Pn));
        return true;
    }
} // namespace

MTcpSink::MTcpSink() = default;

MTcpSink::~MTcpSink() {
    Close();
}

void MTcpSink::CloseLocked() {
    if (SockFd >= 0) {
        ::close(SockFd);
        SockFd = -1;
    }
}

bool MTcpSink::Open() {
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (bOpen)
        return true;
    sockaddr_in Addr;
    if (!ResolveTarget(Target, Addr))
        return false;
    SockFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (SockFd < 0)
        return false;
    if (::connect(SockFd, reinterpret_cast<sockaddr*>(&Addr), sizeof(Addr)) != 0) {
        ::close(SockFd);
        SockFd = -1;
        return false;
    }
    bOpen             = true;
    NextReconnectAtMs = 0;
    return true;
}

void MTcpSink::Close() {
    std::lock_guard<std::mutex> Lock(WriteMutex);
    CloseLocked();
    bOpen = false;
}

void MTcpSink::SendLocked(const char* Ptr, size_t Bytes) {
    if (SockFd < 0)
        return;
    size_t Sent = 0;
    while (Sent < Bytes) {
        const ssize_t N = ::send(SockFd, Ptr + Sent, Bytes - Sent, MSG_NOSIGNAL);
        if (N < 0) {
            if (errno == EINTR)
                continue;
            // 阻塞 socket 上出现 EPIPE / ECONNRESET / EAGAIN:直接丢弃这条。
            MLogMetrics::IncDroppedOverflow();
            CloseLocked();
            return;
        }
        Sent += static_cast<size_t>(N);
    }
    MLogMetrics::AddWrittenBytesTcp(static_cast<uint64>(Bytes));
}

void MTcpSink::WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer) {
    if (Batch.empty() || OutBuffer.empty())
        return;

    std::lock_guard<std::mutex> Lock(WriteMutex);

    if (!bOpen) {
        // 遵守重连退避窗口。
        if (NowSteadyMs() < NextReconnectAtMs) {
            MLogMetrics::IncDroppedOverflow();
            return;
        }
        sockaddr_in Addr;
        if (!ResolveTarget(Target, Addr))
            return;
        SockFd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (SockFd < 0) {
            NextReconnectAtMs = NowSteadyMs() + ReconnectBackoffMs;
            return;
        }
        if (::connect(SockFd, reinterpret_cast<sockaddr*>(&Addr), sizeof(Addr)) != 0) {
            ::close(SockFd);
            SockFd            = -1;
            NextReconnectAtMs = NowSteadyMs() + ReconnectBackoffMs;
            return;
        }
        bOpen = true;
    }

    for (const SLogRecord& R : Batch) {
        const ELogLevel Lvl = static_cast<ELogLevel>(R.Level);
        if (static_cast<int>(Lvl) < static_cast<int>(MinLevelValue))
            continue;
        const size_t N = BuildJsonLine(R, OutBuffer.data(), OutBuffer.size());
        if (N == 0)
            continue;
        // 统计字节数时减掉结尾 '\n':它属于 OutBuffer 的用户态空间,不是线上
        // 载荷;但 MLogMetrics::AddWrittenBytesTcp 报的是传给 send() 的长度
        // (含它),以与 File sink 的口径一致。
        SendLocked(OutBuffer.data(), N);
        if (SockFd < 0) {
            // 发送失败且连接已关闭:剩下的记录只能丢弃。
            MLogMetrics::IncDroppedOverflow();
            return;
        }
    }
}

void MTcpSink::Flush() {
    // 没有用户态缓冲 —— 每次 send 都立即发出,故此处故意什么都不做。
}