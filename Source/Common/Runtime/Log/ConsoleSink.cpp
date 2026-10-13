/**
 * @file ConsoleSink.cpp
 * @brief MConsoleSink 实现 —— 把日志记录格式化成人读的一行文本写 stdout。
 */
#include "Common/Runtime/Log/ConsoleSink.h"
#include "Common/Runtime/Json.h"
#include "Common/Runtime/Log/LogRegistry.h"
#include "Common/Runtime/Log/LogStringTable.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>

namespace {
    /**
     * @brief 把 SLogRecord 的纳秒时间戳格式化为 ISO-8601 UTC 字符串。
     *
     * 这里允许做墙钟换算:时间戳只用于人读展示(规范用 steady_clock 只是
     * 为了让时间戳单调,并不限制格式化层事后换算成墙钟)。调用方 Buffer
     * 须 ≥32 字节;32 字节 scratch 比 24 字节最坏情况多一字节,避免 GCC 的
     * -Wformat-truncation 在调用点做出悲观假设。
     */
    void FormatIsoTimestamp(uint64 TimestampNs, char* Buffer, size_t BufferSize) {
        using namespace std::chrono;
        const auto        Sec    = duration_cast<seconds>(nanoseconds(TimestampNs));
        const auto        FracNs = TimestampNs - duration_cast<nanoseconds>(Sec).count();
        const std::time_t TimeT  = static_cast<std::time_t>(Sec.count());
        std::tm           Tm{};
#if defined(_MSC_VER)
        gmtime_s(&Tm, &TimeT);
#else
        gmtime_r(&TimeT, &Tm);
#endif
        // 毫秒小数部分(人读 3 位足够)
        const int Millis = static_cast<int>(FracNs / 1'000'000);
        // 64 字节 scratch 承载格式化结果,再用单个 %s 拷进 Buffer —— 这样
        // 调用点的长度分析只需处理一个上界明确的 %s。
        const MString FmtBuf = MFormat::Format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z", Tm.tm_year + 1900, Tm.tm_mon + 1, Tm.tm_mday, Tm.tm_hour, Tm.tm_min, Tm.tm_sec, Millis);
        if (BufferSize > 0) {
            const size_t Copy = (FmtBuf.size() >= BufferSize) ? (BufferSize - 1) : FmtBuf.size();
            std::memcpy(Buffer, FmtBuf.data(), Copy);
            Buffer[Copy] = '\0';
        }
    }

    /**
     * @brief 从 SLogRecord 取出消息正文。
     *
     * Inline 分支(短载荷,Flags bit0 = 0)的字节直接内嵌在记录里;Overflow
     * 分支的存活缓冲由 dispatcher 调用方经上下文快照传入 —— 这里把任何置了
     * bit0 的记录都按「尽力可打印」处理(仍然输出其中已存在的内嵌字节)。
     */
    void CopyMessage(const SLogRecord& Record, char* Out, size_t OutSize) {
        const size_t Max = sizeof(Record.Payload.Inline.Data);
        size_t       Len = 0;
        while (Len < Max && Record.Payload.Inline.Data[Len] != '\0') {
            ++Len;
        }
        if (Len >= OutSize) {
            Len = OutSize - 1;
        }
        std::memcpy(Out, Record.Payload.Inline.Data, Len);
        Out[Len] = '\0';
    }

    /**
     * @brief 由紧凑的 CategoryId 反查分类名,越界时安全回退。
     *
     * 例如绕过注册表直接构造的记录,其 Id 可能没有对应分类。
     */
    const char* ResolveCategoryName(uint16 CategoryId) {
        const SLogCategory* Cat = MLogRegistry::Get().GetById(CategoryId);
        return (Cat && Cat->Name) ? Cat->Name : "?";
    }

    /**
     * @brief 把 file/function 的字符串表 id 反查回实际字符串。
     *
     * 无效 id(0 或未驻留)回退为空串。
     */
    const char* ResolveInterned(uint32 Id) {
        const char* Str = MLogStringTable::Get(Id);
        return (Str && Str[0]) ? Str : "";
    }

    /**
     * @brief 用 snprintf 把整数追加进字符缓冲,返回新的写游标。
     *
     * 前置条件:调用时 *Cursor 处已是以 NUL 结尾的字符串。
     */
    void Append(char*& Cursor, char* End, const char* Fmt, ...) {
        if (Cursor >= End)
            return;
        va_list Ap;
        va_start(Ap, Fmt);
        const int N = std::vsnprintf(Cursor, static_cast<size_t>(End - Cursor), Fmt, Ap);
        va_end(Ap);
        if (N < 0) {
            return;
        }
        const int Advance = (N >= static_cast<int>(End - Cursor)) ? static_cast<int>(End - Cursor) - 1 : N;
        Cursor += Advance;
    }
} // namespace

bool MConsoleSink::Open() {
    std::lock_guard<std::mutex> Lock(WriteMutex);
    bOpen = true;
    return true;
}

void MConsoleSink::Close() {
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (bOpen) {
        std::cout.flush();
    }
    bOpen = false;
}

void MConsoleSink::WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer) {
    if (Batch.empty() || OutBuffer.empty()) {
        return;
    }

    /**
     * @brief 逐条把记录序列化进 OutBuffer,并整行写 stdout。
     *
     * 每写完一行就重置游标。调用方保证 OutBuffer 至少装得下一行;单条消息
     * 超出缓冲时会被截断 —— 与既有 MLogger::FormatLine 的行为一致。
     */
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (!bOpen) {
        return;
    }

    char* const BufBegin = OutBuffer.data();
    char* const BufEnd   = BufBegin + OutBuffer.size();

    for (const SLogRecord& R : Batch) {
        const ELogLevel Lvl = static_cast<ELogLevel>(R.Level);
        if (static_cast<int>(Lvl) < static_cast<int>(MinLevelValue)) {
            continue;
        }

        char Timestamp[32];
        FormatIsoTimestamp(R.TimestampNs, Timestamp, sizeof(Timestamp));

        char Message[128];
        CopyMessage(R, Message, sizeof(Message));

        const char*  CatName = ResolveCategoryName(R.CategoryId);
        const char*  FileStr = ResolveInterned(R.FileStringId);
        const uint16 Line    = R.Line;

        char* Cursor = BufBegin;
        Append(Cursor, BufEnd, "[%s] [%s] [%s] [%s:%u] %s\n", Timestamp, LogLevelToString(Lvl), CatName, (FileStr && FileStr[0]) ? FileStr : "?", static_cast<unsigned>(Line), Message);

        // 把整行写给 stdout。snprintf 已把结尾 '\n' 写进缓冲,一次 write()
        // 就能带出去 —— 若拆成 write() + put('\n') 两次系统调用,每条记录的
        // 开销翻倍。std::endl 会强制每行 flush,这里刻意避开(flush 统一由
        // 下面的 Flush() 把关)。
        const size_t Len = static_cast<size_t>(Cursor - BufBegin);
        if (Len > 0) {
            std::cout.write(BufBegin, static_cast<std::streamsize>(Len));
        }
    }
}

void MConsoleSink::Flush() {
    std::lock_guard<std::mutex> Lock(WriteMutex);
    std::cout.flush();
}
