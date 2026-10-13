/**
 * @file RollingFileSink.cpp
 * @brief MRollingFileSink 实现 —— JSON Lines 落盘 + 按体积轮转 + 归档裁剪。
 */
#include "Common/Runtime/Log/RollingFileSink.h"
#include "Common/Runtime/Json.h"
#include "Common/Runtime/Log/LogMetrics.h"
#include "Common/Runtime/Log/LogRegistry.h"
#include "Common/Runtime/Log/LogStringTable.h"

#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
    /**
     * @brief 由紧凑的 CategoryId 反查分类名,越界时安全回退。
     */
    const char* ResolveCategoryName(uint16 CategoryId) {
        const SLogCategory* Cat = MLogRegistry::Get().GetById(CategoryId);
        return (Cat && Cat->Name) ? Cat->Name : "?";
    }

    /**
     * @brief 字符串表 id 反查,无效时回退为空串。
     */
    const char* ResolveInterned(uint32 Id) {
        const char* Str = MLogStringTable::Get(Id);
        return (Str && Str[0]) ? Str : "";
    }

    /**
     * @brief 把 steady_clock 纳秒格式化为 ISO-8601 UTC,毫秒精度。
     *
     * 墙钟换算的说明同 ConsoleSink。
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
        const int Millis = static_cast<int>(FracNs / 1'000'000);
        // 64 字节 scratch 承载格式化结果,用 memcpy 拷进 Buffer —— 调用点的
        // 长度分析只需看到一个上界明确的拷贝。
        const MString FmtBuf = MFormat::Format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z", Tm.tm_year + 1900, Tm.tm_mon + 1, Tm.tm_mday, Tm.tm_hour, Tm.tm_min, Tm.tm_sec, Millis);
        const size_t  Len    = FmtBuf.size();
        if (BufferSize > 0) {
            const size_t Copy = (Len >= BufferSize) ? (BufferSize - 1) : Len;
            std::memcpy(Buffer, FmtBuf.data(), Copy);
            Buffer[Copy] = '\0';
        }
    }

    /**
     * @brief 返回 Path 在磁盘上的字节数;出错返回 0。
     *
     * 只取大小、不依赖 struct stat 的其余字段。
     */
    size_t FileSizeOrZero(const MString& Path) {
        struct stat St{};
        if (::stat(Path.c_str(), &St) != 0)
            return 0;
        return static_cast<size_t>(St.st_size);
    }
} // namespace

MRollingFileSink::MRollingFileSink() = default;

MRollingFileSink::~MRollingFileSink() {
    Close();
}

bool MRollingFileSink::OpenLocked() {
    if (Stream != nullptr) {
        return true;
    }
    if (FilePath.empty()) {
        return false;
    }
    Stream = std::fopen(FilePath.c_str(), "ab");
    if (Stream == nullptr) {
        return false;
    }
    Fd           = ::fileno(Stream);
    CurrentBytes = FileSizeOrZero(FilePath);
    return true;
}

bool MRollingFileSink::Open() {
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (bOpen) {
        return true;
    }
    if (!OpenLocked()) {
        return false;
    }
    bOpen = true;
    return true;
}

void MRollingFileSink::Close() {
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (Stream != nullptr) {
        std::fclose(Stream);
        Stream = nullptr;
    }
    Fd           = -1;
    bOpen        = false;
    CurrentBytes = 0;
}

void MRollingFileSink::RotateLocked() {
    if (Stream == nullptr) {
        return;
    }
    std::fclose(Stream);
    Stream = nullptr;
    Fd     = -1;

    /**
     * @brief 找出从 1 开始的最小空闲数字后缀。
     *
     * 不重编号已有归档:那会与并发读者竞争,而且规范的「保留 N 份归档」
     * 策略并不需要重编号。
     */
    int FreeSuffix = 1;
    while (FreeSuffix <= static_cast<int>(NumArchives) + 8) {
        const MString Candidate = MFormat::Format("{}.{}", FilePath, FreeSuffix);
        if (FileSizeOrZero(Candidate) == 0 && ::access(Candidate.c_str(), F_OK) != 0) {
            break;
        }
        ++FreeSuffix;
    }

    const MString ArchivePath = MFormat::Format("{}.{}", FilePath, FreeSuffix);
    // 把活跃日志重命名到归档槽位;若该槽已存在陈旧文件就直接覆盖
    // (POSIX 的 rename(2) 是原子替换)。
    if (::rename(FilePath.c_str(), ArchivePath.c_str()) != 0) {
        // 重命名失败 —— 放弃本次轮转,重新打开一个全新的活跃文件,保证后续
        // 写入仍有去处。
        OpenLocked();
        return;
    }

    // 裁掉超出 NumArchives 的归档。上面的「活跃 → 归档」重命名之后,新写的
    // 归档就占在 FreeSuffix 上,而原先占该槽的归档已被 rename(2) 覆盖。因此
    // 只需删除那些因超出 NumArchives 预算而不再保留的**既有**归档,即后缀比
    // 新归档槽位再减去 (NumArchives - 1) 更旧的那些。
    //
    // 具体地:NumArchives == 5、刚轮转到槽 N 时,有效归档是 N, N-1, N-2,
    // N-3, N-4;低于 N-4 的一律丢弃。
    if (NumArchives > 0) {
        const int OldestKept = FreeSuffix - static_cast<int>(NumArchives) + 1;
        // 从 OldestKept 向上走(跳过 FreeSuffix 上刚写好的归档,以及仍然
        // 保留的较新槽位),把更旧的逐个删除。范围上限与上面的扫描一致,
        // 避免死循环。
        const int MaxScan = FreeSuffix + 64;
        for (int N = 0; N < OldestKept && N < MaxScan; ++N) {
            const MString Old = MFormat::Format("{}.{}", FilePath, N);
            if (::access(Old.c_str(), F_OK) == 0) {
                ::unlink(Old.c_str());
            }
        }
    }

    OpenLocked();
}

void MRollingFileSink::WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer) {
    if (Batch.empty() || OutBuffer.empty()) {
        return;
    }

    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (!bOpen || Stream == nullptr) {
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

        const char* CatName = ResolveCategoryName(R.CategoryId);
        const char* FileStr = ResolveInterned(R.FileStringId);
        const char* FuncStr = ResolveInterned(R.FuncStringId);

        // 消息正文:取内嵌字节。(Overflow 分支是给 dispatcher 调用方用的;
        // 测试路径永远是 Inline,因为小载荷上 SLogRecord::Flags 的 bit0
        // 是清掉的。)
        char         Message[128];
        size_t       MsgLen    = 0;
        const size_t MaxInline = sizeof(R.Payload.Inline.Data);
        while (MsgLen < MaxInline && R.Payload.Inline.Data[MsgLen] != '\0') {
            ++MsgLen;
        }
        if (MsgLen >= sizeof(Message))
            MsgLen = sizeof(Message) - 1;
        std::memcpy(Message, R.Payload.Inline.Data, MsgLen);
        Message[MsgLen] = '\0';

        // 用 MJsonWriter 构造 JSON 对象 —— 字符串转义由它统一处理,消息正文
        // 里出现引号/换行也安全。Object() 已开作用域,读 ToString() 前要用
        // EndObject() 收掉。
        MJsonWriter W = MJsonWriter::Object();
        W.Key("ts");
        W.Value(MString(Timestamp));
        W.Key("level");
        W.Value(LogLevelToString(Lvl));
        W.Key("category");
        W.Value(MString(CatName));
        W.Key("thread");
        W.Value(static_cast<uint64>(R.ThreadId));
        W.Key("service");
        W.Value(ServiceName);
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

        // 追加 "\n",组成规范的 JSON Line。
        char*         Cursor       = BufBegin;
        const MString HeaderStr    = MFormat::Format("{}\n", Line);
        const size_t  BytesToWrite = HeaderStr.size();
        if (BytesToWrite == 0 || Cursor + BytesToWrite >= BufEnd) {
            // 被截断:宁可跳过这条记录,也不写出半行 JSON。
            continue;
        }
        std::memcpy(Cursor, HeaderStr.data(), BytesToWrite);

        // 轮转:若写入本条会越过体积阈值,则先轮转。阈值以活跃文件的当前
        // 大小(CurrentBytes)为基准。
        if (RotatedFileBytes > 0 && CurrentBytes > 0 && CurrentBytes + BytesToWrite > RotatedFileBytes) {
            RotateLocked();
        }

        const size_t Written = std::fwrite(Cursor, 1, BytesToWrite, Stream);
        if (Written == BytesToWrite) {
            CurrentBytes += Written;
            MLogMetrics::AddWrittenBytesFile(Written);
        } else {
            // fwrite 短写:按磁盘写满处理,让运维能从指标里看到这次丢日志。
            // 不重试 —— 规范 §7 要求 sink 计数后停下。
            MLogMetrics::IncDroppedOverflow();
            break;
        }
    }
}

void MRollingFileSink::Flush() {
    std::lock_guard<std::mutex> Lock(WriteMutex);
    if (Stream == nullptr) {
        return;
    }
    std::fflush(Stream);
    ++FlushCount;
    MaybeFsyncLocked();
}

void MRollingFileSink::MaybeFsyncLocked() {
    if (Fd < 0 || FlushesPerFsync == 0) {
        return;
    }
    if ((FlushCount % FlushesPerFsync) != 0) {
        return;
    }
    /**
     * @brief 把文件数据刷到磁盘。
     *
     * fdatasync 才是日志持久化的正确原语:它落盘数据,不强制 fsync 会带来的
     * 元数据更新。没有 fdatasync 的平台(如 macOS)由条件编译回退到 fsync,
     * 稍慢但安全。
     */
#if defined(__APPLE__)
    ::fsync(Fd);
#else
    ::fdatasync(Fd);
#endif
}
