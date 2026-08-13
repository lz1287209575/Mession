#include "Common/Runtime/Log/ConsoleSink.h"
#include "Common/Runtime/Log/LogStringTable.h"
#include "Common/Runtime/Log/LogRegistry.h"
#include "Common/Runtime/Json.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>

namespace
{
    // Format the SLogRecord's nanosecond timestamp as ISO-8601 UTC.
    // Wall-clock conversion is acceptable here because timestamps are only
    // used for human-readable display (the spec uses steady_clock only to
    // make them monotonic, not because the formatting layer can't move to
    // wall-clock after the fact). `Buffer` must be at least 32 bytes.
    // 32-byte scratch sits one over the 24-byte worst case so GCC's
    // -Wformat-truncation doesn't pessimize the call site (Buffer at the
    // caller is also ≥32).
    void FormatIsoTimestamp(uint64 TimestampNs, char* Buffer, size_t BufferSize)
    {
        using namespace std::chrono;
        const auto Sec = duration_cast<seconds>(nanoseconds(TimestampNs));
        const auto FracNs = TimestampNs - duration_cast<nanoseconds>(Sec).count();
        const std::time_t TimeT = static_cast<std::time_t>(Sec.count());
        std::tm Tm{};
#if defined(_MSC_VER)
        gmtime_s(&Tm, &TimeT);
#else
        gmtime_r(&TimeT, &Tm);
#endif
        // millisecond fraction (3 digits is enough for human display)
        const int Millis = static_cast<int>(FracNs / 1'000'000);
        // 64-byte scratch carries the formatted string; copy into Buffer
        // with a single %s so the call-site size analysis only has to handle
        // a single %s of bounded upper length.
        const MString FmtBuf = MFormat::Format(
            "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z",
            Tm.tm_year + 1900, Tm.tm_mon + 1, Tm.tm_mday,
            Tm.tm_hour, Tm.tm_min, Tm.tm_sec, Millis);
        if (BufferSize > 0)
        {
            const size_t Copy = (FmtBuf.size() >= BufferSize) ? (BufferSize - 1) : FmtBuf.size();
            std::memcpy(Buffer, FmtBuf.data(), Copy);
            Buffer[Copy] = '\0';
        }
    }

    // Pull the message text from an SLogRecord. For the Inline branch
    // (≤16B, Flags bit0 = 0) the bytes are embedded directly in the record.
    // For Overflow the dispatcher's caller passes the live buffer via the
    // context snapshot — here we treat any record with bit0 set as
    // best-effort printable (we still emit whatever inline bytes are present).
    void CopyMessage(const SLogRecord& Record, char* Out, size_t OutSize)
    {
        const size_t Max = sizeof(Record.Payload.Inline.Data);
        size_t Len = 0;
        while (Len < Max && Record.Payload.Inline.Data[Len] != '\0')
        {
            ++Len;
        }
        if (Len >= OutSize)
        {
            Len = OutSize - 1;
        }
        std::memcpy(Out, Record.Payload.Inline.Data, Len);
        Out[Len] = '\0';
    }

    // Resolve category name from its compact Id, with safe fallback for
    // out-of-range ids (e.g. records that bypassed the registry).
    const char* ResolveCategoryName(uint16 CategoryId)
    {
        const SLogCategory* Cat = MLogRegistry::Get().GetById(CategoryId);
        return (Cat && Cat->Name) ? Cat->Name : "?";
    }

    // Resolve a file/function string id against the intern table, falling
    // back to "" for the invalid id (0 / not interned).
    const char* ResolveInterned(uint32 Id)
    {
        const char* Str = MLogStringTable::Get(Id);
        return (Str && Str[0]) ? Str : "";
    }

    // Append an integer to a char buffer using snprintf, returning the new
    // write cursor. The buffer must already be null-terminated at *Cursor.
    void Append(char*& Cursor, char* End, const char* Fmt, ...)
    {
        if (Cursor >= End) return;
        va_list Ap;
        va_start(Ap, Fmt);
        const int N = std::vsnprintf(Cursor, static_cast<size_t>(End - Cursor), Fmt, Ap);
        va_end(Ap);
        if (N < 0)
        {
            return;
        }
        const int Advance = (N >= static_cast<int>(End - Cursor))
            ? static_cast<int>(End - Cursor) - 1
            : N;
        Cursor += Advance;
    }
}

bool MConsoleSink::Open()
{
    std::lock_guard<std::mutex> Lock(WriteMutex);
    bOpen = true;
    return true;
}

void MConsoleSink::Close()
{
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (bOpen)
    {
        std::cout.flush();
    }
    bOpen = false;
}

void MConsoleSink::WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer)
{
    if (Batch.empty() || OutBuffer.empty())
    {
        return;
    }

    // We serialize one record at a time into OutBuffer and write each
    // formatted line to stdout before resetting the cursor. The OutBuffer
    // is sized large enough by the caller to hold at least one line; if a
    // single message is longer than the buffer it gets truncated, which is
    // the same behavior the existing MLogger FormatLine produces.
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (!bOpen)
    {
        return;
    }

    char* const BufBegin = OutBuffer.data();
    char* const BufEnd   = BufBegin + OutBuffer.size();

    for (const SLogRecord& R : Batch)
    {
        const ELogLevel Lvl = static_cast<ELogLevel>(R.Level);
        if (static_cast<int>(Lvl) < static_cast<int>(MinLevelValue))
        {
            continue;
        }

        char Timestamp[32];
        FormatIsoTimestamp(R.TimestampNs, Timestamp, sizeof(Timestamp));

        char Message[128];
        CopyMessage(R, Message, sizeof(Message));

        const char* CatName = ResolveCategoryName(R.CategoryId);
        const char* FileStr = ResolveInterned(R.FileStringId);
        const uint16 Line   = R.Line;

        char* Cursor = BufBegin;
        Append(Cursor, BufEnd, "[%s] [%s] [%s] [%s:%u] %s\n",
            Timestamp,
            LogLevelToString(Lvl),
            CatName,
            (FileStr && FileStr[0]) ? FileStr : "?",
            static_cast<unsigned>(Line),
            Message);

        // Write the formatted line to stdout. snprintf already appended the
        // trailing '\n' into the buffer, so a single write() carries it
        // out — two write() calls (write + put('\n')) would double the
        // per-record syscall cost. std::endl would force a flush per
        // line, which we explicitly avoid (Flush() below is the gate).
        const size_t Len = static_cast<size_t>(Cursor - BufBegin);
        if (Len > 0)
        {
            std::cout.write(BufBegin, static_cast<std::streamsize>(Len));
        }
    }
}

void MConsoleSink::Flush()
{
    std::lock_guard<std::mutex> Lock(WriteMutex);
    std::cout.flush();
}
