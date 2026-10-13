/**
 * @file RollingFileSink.h
 * @brief MRollingFileSink —— 新管线(规范 §5.8)滚动 JSON-Lines 文件 sink。
 *
 * @detail 每次 Open() 以追加模式打开 FilePath。WriteBatch 把每条记录
 * 序列化为一个 JSON 对象,连同 '\n' 分隔符一起 fwrite —— 整批在 Unix
 * 上只需一次系统调用的批量路径。
 *
 * 滚动策略:当前文件磁盘大小加上下一条记录大小将超过
 * RotatedFileBytes 时,关闭当前文件、加数字后缀重命名、删除超出
 * NumArchives 的最旧归档、重新打开新文件。滚动粒度为"每条记录"
 * (而非每行),因此单条超大记录最多越过阈值一个记录的字节量。
 *
 * Flush() 每次都调 fflush(),并每累计 FlushesPerFsync 次(默认 10)
 * 追加一次 fdatasync(),在持久性与吞吐之间取平衡。
 *
 * ServiceName 会写进 JSON 载荷(滚动时也进归档名)。FilePath 是活动
 * 日志路径;归档加数字后缀且与活动文件同目录。
 */
#pragma once

#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/MLib.h"
#include <cstdio>
#include <mutex>


class MRollingFileSink : public ILogSink {
    public:
    MRollingFileSink();
    ~MRollingFileSink() override;

    // 新管线
    bool      Open() override;
    void      Close() override;
    void      WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer) override;
    void      Flush() override;
    ELogLevel MinLevel() const override {
        return MinLevelValue;
    }
    const char* Name() const override {
        return "file";
    }

    // 配置(公开字段便于集成;文档约定的用法是直接赋值成员——
    // 见 EchoService 示例)。必须在 Open() 之前设置。
    MString ServiceName;                                   // e.g. "GatewayServer"
    MString FilePath;                                      // e.g. "/tmp/test-mession-log.jsonl"
    size_t  RotatedFileBytes = 100ull * 1024ull * 1024ull; // 100MB default
    size_t  NumArchives      = 5;
    uint32  FlushesPerFsync  = 10;

    void SetMinLevel(ELogLevel Level) {
        MinLevelValue = Level;
    }

    private:
    ELogLevel  MinLevelValue = ELogLevel::Trace;
    FILE*      Stream        = nullptr;
    size_t     CurrentBytes  = 0;
    uint32     FlushCount    = 0;
    bool       bOpen         = false;
    int        Fd            = -1;
    std::mutex WriteMutex;

    // 滚动:关闭 Stream、重命名为 <path>.<N>、超出 NumArchives 时
    // 删最旧归档、再重开 Stream。调用方须持有 WriteMutex。
    void RotateLocked();

    // 打开 Stream 并以现有磁盘大小初始化 CurrentBytes(设计上跨运行
    // 只追加)。调用方须持有 WriteMutex。
    bool OpenLocked();

    // 计数达到阈值时对底层 fd 发 fdatasync。由 Flush() 在持有
    // WriteMutex 时调用。
    void MaybeFsyncLocked();
};
