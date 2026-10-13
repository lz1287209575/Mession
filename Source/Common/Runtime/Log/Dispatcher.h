#pragma once
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/MpscRingBuffer.h"
#include "Common/Runtime/MLib.h"

#include <atomic>
#include <thread>
#include <vector>

class MLogSinkWriter;

/**
 * @file Dispatcher.h
 * @brief MLogDispatcher —— 单消费者分发线程(规范 §5.6),位于共享
 * MPSC 环形队列与各 sink writer 之间。
 *
 * @detail 按批弹出记录,依 SinkMask 分区,把每个分区交给对应的
 * MLogSinkWriter。
 *
 * 为何"读时分区"而非"写时分区":生产方调用点需要以最小锁代价入队。
 * 读时分区使入队只需一次原子 slot 抢占;代价——把记录拷进各 sink 的
 * 暂存区——只由这一个线程承担,不会给热路径增加竞争。
 */
class MLogDispatcher {
    public:
    MLogDispatcher() = default;
    ~MLogDispatcher();

    /**
     * @brief 把 dispatcher 接到输入环形队列与 writer 集合。
     * @param InQueue 输入环形队列(不持有所有权,生命周期须长于 dispatcher)
     * @param InWriters writer 指针数组(同上,外部持有存储)
     */
    void Configure(TMpscRingBuffer<SLogRecord>* InQueue, TVector<MLogSinkWriter*>* InWriters);

    void Start();
    void Stop();

    // 可选:调整分区暂存大小,默认 256 条。
    void SetBatchSize(size_t InBatchSize) {
        BatchSize = InBatchSize;
    }

    private:
    TMpscRingBuffer<SLogRecord>* Queue     = nullptr;
    TVector<MLogSinkWriter*>*    Writers   = nullptr;
    size_t                       BatchSize = 256;

    std::thread       Worker;
    std::atomic<bool> bRunning{false};

    void RunLoop();
};