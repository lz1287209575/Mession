#pragma once
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/MLib.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

/**
 * @file SinkWriter.h
 * @brief MLogSinkWriter —— 每个 ILogSink 一个写线程(规范 §5.7)。
 *
 * @detail 生产侧:MLogDispatcher 经 EnqueueBatch() 推入批次(入口是
 * 一个小型受 mutex + cv 保护的 MPSC 收件箱;因 ILogSink 一般不可移动
 * 构造,无法做到完全无锁,只能移动 TSinkBatch 包装)。dispatcher 的
 * 热路径就是 EnqueueBatch,锁竞争面是收件箱互斥量,而非 sink 本身。
 *
 * 消费侧:RunLoop() 取批次,套用配置的 EFlushPolicy,再调 Sink->WriteBatch。
 * 各 sink 自行负责序列化(Console 出文本,file/UDP/TCP 出 JSON Lines),
 * 写线程无需知道线上格式。
 *
 * 生命周期:Start() 起线程;Stop() 置 bRunning=false、唤醒 cv 并 join。
 * Stop() 返回后收件箱已空,该写线程不会再发生任何 Sink->* 调用。
 */
class MLogSinkWriter {
    public:
    struct SSinkBatch {
        TVector<SLogRecord> Records;
    };

    MLogSinkWriter() = default;
    ~MLogSinkWriter();

    /**
     * @brief 绑定 sink。必须在 Start() 之前调用。
     * @param InSink sink 指针(不持有所有权;调用方须保证其存活到
     * Stop() 返回之后)
     * @param InPolicy 刷新策略,默认 IntervalOrSize
     */
    void SetSink(ILogSink* InSink, EFlushPolicy InPolicy = EFlushPolicy::IntervalOrSize);

    /**
     * @brief 热路径入口:把批次交给写线程。
     * @param Batch 一批待写记录
     * @detail 收件箱满(有界小队列,容量 16 批)时会阻塞,这是
     * 有文档记录的背压信号。
     */
    void EnqueueBatch(TSpan<const SLogRecord> Batch);

    void Start();
    void Stop();

    void SetFlushIntervalMs(int Ms) {
        FlushIntervalMs = Ms;
    }
    void SetFlushSizeThreshold(int B) {
        FlushSizeThreshold = B;
    }

    /**
     * @brief 同步排空收件箱并驱动 sink 直至写完。
     * @detail 供 MLog::Shutdown() 使用,确保关键路径记录在写线程
     * 退出前已上线。
     */
    void FlushSync();

    private:
    // 收件箱在 EnqueueBatch 开始阻塞前可容纳的最大批次数。
    // 该容量在唤醒延迟与写线程压力之间取平衡。
    static constexpr size_t kMaxBatches = 16;

    ILogSink*    Sink               = nullptr;
    EFlushPolicy Policy             = EFlushPolicy::IntervalOrSize;
    int          FlushIntervalMs    = 10;
    int          FlushSizeThreshold = 64 * 1024;

    std::thread                    Worker;
    std::mutex                     InboxMutex;
    std::condition_variable        InboxCv;
    TQueue<TSharedPtr<SSinkBatch>> Inbox;
    std::atomic<bool>              bRunning{false};

    // 自上次 Flush() 以来累积的字节数,用于 SizeThreshold /
    // IntervalOrSize 策略判断。
    int       BufferedBytes = 0;
    long long LastFlushMs   = 0;

    void RunLoop();
    void MaybeFlush(bool bForce);
};
