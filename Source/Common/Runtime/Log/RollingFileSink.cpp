#include "Common/Runtime/Log/RollingFileSink.h"
#include "Common/Runtime/Log/LogStringTable.h"
#include "Common/Runtime/Log/LogRegistry.h"
#include "Common/Runtime/Log/LogMetrics.h"
#include "Common/Runtime/Json.h"

#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
    // Resolve category name from a compact Id with safe fallback.
    const char* ResolveCategoryName(uint16 CategoryId)
    {
        const SLogCategory* Cat = MLogRegistry::Get().GetById(CategoryId);
        return (Cat && Cat->Name) ? Cat->Name : "?";
    }

    // Resolve a string-table id with safe fallback to "".
    const char* ResolveInterned(uint32 Id)
    {
        const char* Str = MLogStringTable::Get(Id);
        return (Str && Str[0]) ? Str : "";
    }

    // Format the steady_clock nanoseconds as an ISO-8601 UTC string with
    // millisecond resolution. Same wall-clock note as ConsoleSink.
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
        const int Millis = static_cast<int>(FracNs / 1'000'000);
        // 64-byte scratch carries the formatted string; copy into Buffer
        // with memcpy so the call-site size analysis only sees a single
        // bounded copy.
        const MString FmtBuf = MFormat::Format(
            "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z",
            Tm.tm_year + 1900, Tm.tm_mon + 1, Tm.tm_mday,
            Tm.tm_hour, Tm.tm_min, Tm.tm_sec, Millis);
        const size_t Len = FmtBuf.size();
        if (BufferSize > 0)
        {
            const size_t Copy = (Len >= BufferSize) ? (BufferSize - 1) : Len;
            std::memcpy(Buffer, FmtBuf.data(), Copy);
            Buffer[Copy] = '\0';
        }
    }

    // Return the on-disk size of `Path`, or 0 on error. Cheaper than stat()
    // for the typical case because we don't need most of struct stat.
    size_t FileSizeOrZero(const MString& Path)
    {
        struct stat St{};
        if (::stat(Path.c_str(), &St) != 0) return 0;
        return static_cast<size_t>(St.st_size);
    }
}

MRollingFileSink::MRollingFileSink() = default;

MRollingFileSink::~MRollingFileSink()
{
    Close();
}

bool MRollingFileSink::OpenLocked()
{
    if (Stream != nullptr)
    {
        return true;
    }
    if (FilePath.empty())
    {
        return false;
    }
    Stream = std::fopen(FilePath.c_str(), "ab");
    if (Stream == nullptr)
    {
        return false;
    }
    Fd = ::fileno(Stream);
    CurrentBytes = FileSizeOrZero(FilePath);
    return true;
}

bool MRollingFileSink::Open()
{
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (bOpen)
    {
        return true;
    }
    if (!OpenLocked())
    {
        return false;
    }
    bOpen = true;
    return true;
}

void MRollingFileSink::Close()
{
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (Stream != nullptr)
    {
        std::fclose(Stream);
        Stream = nullptr;
    }
    Fd = -1;
    bOpen = false;
    CurrentBytes = 0;
}

void MRollingFileSink::RotateLocked()
{
    if (Stream == nullptr)
    {
        return;
    }
    std::fclose(Stream);
    Stream = nullptr;
    Fd = -1;

    // Find the lowest free numeric suffix starting at 1. We do not
    // re-number existing archives — that would race with concurrent
    // readers and is unnecessary for the spec's "keep N archives" policy.
    int FreeSuffix = 1;
    while (FreeSuffix <= static_cast<int>(NumArchives) + 8)
    {
        const MString Candidate = MFormat::Format("{}.{}",
            FilePath, FreeSuffix);
        if (FileSizeOrZero(Candidate) == 0 &&
            ::access(Candidate.c_str(), F_OK) != 0)
        {
            break;
        }
        ++FreeSuffix;
    }

    const MString ArchivePath = MFormat::Format("{}.{}",
        FilePath, FreeSuffix);
    // rename the live log to the archive slot; if a stale file lives there,
    // overwrite it (rename(2) atomically replaces on POSIX).
    if (::rename(FilePath.c_str(), ArchivePath.c_str()) != 0)
    {
        // Rename failed — give up rotation and reopen a fresh live file
        // so subsequent writes still go somewhere.
        OpenLocked();
        return;
    }

    // Trim archives beyond NumArchives. After the live→archive rename above,
    // the freshly written archive occupies FreeSuffix and any pre-existing
    // archive at that slot has been overwritten by rename(2). We only need
    // to delete *pre-existing* archives that no longer fit under the
    // NumArchives budget, i.e. those with a suffix older than the new
    // archive's slot minus (NumArchives - 1).
    //
    // Concretely: if NumArchives == 5 and we just rotated into slot N,
    // valid archives are N, N-1, N-2, N-3, N-4. Anything below N-4 is dropped.
    if (NumArchives > 0)
    {
        const int OldestKept = FreeSuffix - static_cast<int>(NumArchives) + 1;
        // Walk upward from OldestKept (skipping the freshly written archive
        // at FreeSuffix and any kept newer slots) deleting anything older.
        // Range cap matches the scan above so we can't spin forever.
        const int MaxScan = FreeSuffix + 64;
        for (int N = 0; N < OldestKept && N < MaxScan; ++N)
        {
            const MString Old = MFormat::Format("{}.{}",
                FilePath, N);
            if (::access(Old.c_str(), F_OK) == 0)
            {
                ::unlink(Old.c_str());
            }
        }
    }

    OpenLocked();
}

void MRollingFileSink::WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer)
{
    if (Batch.empty() || OutBuffer.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (!bOpen || Stream == nullptr)
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

        const char* CatName = ResolveCategoryName(R.CategoryId);
        const char* FileStr = ResolveInterned(R.FileStringId);
        const char* FuncStr = ResolveInterned(R.FuncStringId);

        // Message: pull the inline bytes (the Overflow path is for the
        // dispatcher's caller; the test path is always Inline because
        // SLogRecord::Flags bit0 is cleared on small payloads).
        char Message[128];
        size_t MsgLen = 0;
        const size_t MaxInline = sizeof(R.Payload.Inline.Data);
        while (MsgLen < MaxInline && R.Payload.Inline.Data[MsgLen] != '\0')
        {
            ++MsgLen;
        }
        if (MsgLen >= sizeof(Message)) MsgLen = sizeof(Message) - 1;
        std::memcpy(Message, R.Payload.Inline.Data, MsgLen);
        Message[MsgLen] = '\0';

        // Build the JSON object via MJsonWriter. It handles all string
        // escaping for us so the message text can contain quotes / newlines
        // safely. Object() already opens the scope; we close it with
        // EndObject() before reading ToString().
        MJsonWriter W = MJsonWriter::Object();
        W.Key("ts");         W.Value(MString(Timestamp));
        W.Key("level");      W.Value(LogLevelToString(Lvl));
        W.Key("category");   W.Value(MString(CatName));
        W.Key("thread");     W.Value(static_cast<uint64>(R.ThreadId));
        W.Key("service");    W.Value(ServiceName);
        W.Key("file");       W.Value(MString(FileStr));
        W.Key("line");       W.Value(static_cast<uint64>(R.Line));
        W.Key("func");       W.Value(MString(FuncStr));
        W.Key("msg");        W.Value(MString(Message));
        W.EndObject();
        const MString& Line = W.ToString();

        // Append "\n" so the result is a proper JSON Line.
        char* Cursor = BufBegin;
        const MString HeaderStr = MFormat::Format("{}\n", Line);
        const size_t BytesToWrite = HeaderStr.size();
        if (BytesToWrite == 0 || Cursor + BytesToWrite >= BufEnd)
        {
            // Truncated — skip the record rather than write a partial line.
            continue;
        }
        std::memcpy(Cursor, HeaderStr.data(), BytesToWrite);

        // Rotation: if writing this record would push us past the size
        // threshold, rotate first. The threshold check is on the live
        // file's current size (CurrentBytes).
        if (RotatedFileBytes > 0 &&
            CurrentBytes > 0 &&
            CurrentBytes + BytesToWrite > RotatedFileBytes)
        {
            RotateLocked();
        }

        const size_t Written = std::fwrite(Cursor, 1, BytesToWrite, Stream);
        if (Written == BytesToWrite)
        {
            CurrentBytes += Written;
            MLogMetrics::AddWrittenBytesFile(Written);
        }
        else
        {
            // fwrite short write: count as disk-full so the operator sees
            // the drop in metrics. We do not retry — spec §7 says the sink
            // should count and stop.
            MLogMetrics::IncDroppedOverflow();
            break;
        }
    }
}

void MRollingFileSink::Flush()
{
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (Stream == nullptr)
    {
        return;
    }
    std::fflush(Stream);
    ++FlushCount;
    MaybeFsyncLocked();
}

void MRollingFileSink::MaybeFsyncLocked()
{
    if (Fd < 0 || FlushesPerFsync == 0)
    {
        return;
    }
    if ((FlushCount % FlushesPerFsync) != 0)
    {
        return;
    }
    // fdatasync is the right primitive for log durability: it persists
    // file data without forcing metadata updates that fsync would. On
    // platforms without fdatasync (e.g. macOS) the conditional compilation
    // falls back to fsync, which is safe if a touch slower.
#if defined(__APPLE__)
    ::fsync(Fd);
#else
    ::fdatasync(Fd);
#endif
}
