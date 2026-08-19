#include "Common/Runtime/Log/UdpSink.h"
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
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
    // Pull inline bytes out of an SLogRecord as a NUL-terminated string.
    void CopyMessage(const SLogRecord& R, char* Out, size_t OutSize) {
        const size_t Max = sizeof(R.Payload.Inline.Data);
        size_t       Len = 0;
        while (Len < Max && R.Payload.Inline.Data[Len] != '\0') {
            ++Len;
        }
        if (Len >= OutSize) {
            Len = OutSize - 1;
        }
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

    // Build a JSON Line for the record into Out. Returns the number of bytes
    // written (excluding the trailing '\n'), or 0 if the buffer was too small.
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

        const size_t Want = Line.size() + 1; // include '\n'
        if (Want > OutSize) {
            return 0;
        }
        std::memcpy(Out, Line.c_str(), Line.size());
        Out[Line.size()] = '\n';
        return Want;
    }

    // Parse "ip:port" into a sockaddr_in. Returns false on any failure.
    bool ResolveTarget(const MString& Target, sockaddr_in& OutAddr) {
        const size_t Colon = Target.rfind(':');
        if (Colon == MString::npos) {
            return false;
        }
        const MString Host = Target.substr(0, Colon);
        const MString Port = Target.substr(Colon + 1);
        std::memset(&OutAddr, 0, sizeof(OutAddr));
        OutAddr.sin_family = AF_INET;
        if (::inet_pton(AF_INET, Host.c_str(), &OutAddr.sin_addr) != 1) {
            return false;
        }
        const int PortNum = std::atoi(Port.c_str());
        if (PortNum <= 0 || PortNum > 65535) {
            return false;
        }
        OutAddr.sin_port = htons(static_cast<uint16>(PortNum));
        return true;
    }
} // namespace

MUdpSink::MUdpSink() = default;

MUdpSink::~MUdpSink() {
    Close();
}

bool MUdpSink::Open() {
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (bOpen) {
        return true;
    }
    sockaddr_in Addr;
    if (!ResolveTarget(Target, Addr)) {
        return false;
    }
    SockFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (SockFd < 0) {
        return false;
    }
    // We connect() so we can use send() instead of sendto() — saves the
    // kernel a route lookup per call.
    if (::connect(SockFd, reinterpret_cast<sockaddr*>(&Addr), sizeof(Addr)) != 0) {
        ::close(SockFd);
        SockFd = -1;
        return false;
    }
    bOpen = true;
    return true;
}

void MUdpSink::Close() {
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (SockFd >= 0) {
        ::close(SockFd);
        SockFd = -1;
    }
    bOpen = false;
}

void MUdpSink::WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer) {
    if (Batch.empty() || OutBuffer.empty()) {
        return;
    }

    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (!bOpen || SockFd < 0) {
        return;
    }

    char* const BufBegin = OutBuffer.data();
    char* const BufEnd   = BufBegin + OutBuffer.size();
    char*       Cursor   = BufBegin;

    const int Cap = (MaxDatagramBytes > 0 && MaxDatagramBytes < static_cast<int>(OutBuffer.size())) ? MaxDatagramBytes : static_cast<int>(OutBuffer.size());

    auto FlushDatagram = [&]() {
        if (Cursor == BufBegin)
            return;
        const size_t  Bytes = static_cast<size_t>(Cursor - BufBegin);
        const ssize_t Sent  = ::send(SockFd, BufBegin, Bytes, 0);
        if (Sent < 0) {
            MLogMetrics::IncDroppedOverflow();
        } else {
            MLogMetrics::AddWrittenBytesUdp(static_cast<uint64>(Sent));
        }
        Cursor = BufBegin;
    };

    for (const SLogRecord& R : Batch) {
        const ELogLevel Lvl = static_cast<ELogLevel>(R.Level);
        if (static_cast<int>(Lvl) < static_cast<int>(MinLevelValue)) {
            continue;
        }
        if (static_cast<size_t>(Cursor - BufBegin) + 256 > static_cast<size_t>(Cap)) {
            FlushDatagram();
        }
        const size_t N = BuildJsonLine(R, Cursor, static_cast<size_t>(BufEnd - Cursor));
        if (N == 0) {
            continue;
        }
        Cursor += N;
    }
    FlushDatagram();
}

void MUdpSink::Flush() {
    // UDP has no flush semantics; sendto already pushes to the kernel.
}