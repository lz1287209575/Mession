/**
 * @file LogMetrics.h
 * @brief SLogMetricsSnapshot 与 MLogMetrics —— 日志管线各环节的计数器与快照类型。
 *
 * 计数器全部为 atomic<uint64>,可在写入热路径上无锁累加,取快照时逐项 load。
 */
#pragma once
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/MLib.h"
#include <atomic>

struct SLogMetricsSnapshot {
    uint64 Enqueued            = 0;
    uint64 DroppedEvicted      = 0;
    uint64 DroppedOverflow     = 0;
    uint64 BlockedEnqueues     = 0;
    uint64 DispatchedBatches   = 0;
    uint64 WrittenBytesConsole = 0;
    uint64 WrittenBytesFile    = 0;
    uint64 WrittenBytesUdp     = 0;
    uint64 WrittenBytesTcp     = 0;
    uint64 TotalSuppressed     = 0;
    // 按分类的抑制计数器,下标为 SLogCategory::Id。消费者只应索引到
    // MLogRegistry::Get().NumCategories() 为止。因分类数量是动态的,
    // 此处不预分配;真正的 resize 发生在
    // MLogMetrics::IncSuppressedByCategory()。
    TVector<uint64> SuppressedByCategory;
};

class MLogMetrics {
    public:
    static void IncEnqueued() {
        SInc(Enqueued);
    }
    static void IncDroppedEvicted() {
        SInc(DroppedEvicted);
    }
    static void IncDroppedOverflow() {
        SInc(DroppedOverflow);
    }
    static void IncBlockedEnqueues() {
        SInc(BlockedEnqueues);
    }
    static void IncDispatchedBatches() {
        SInc(DispatchedBatches);
    }
    static void AddWrittenBytesConsole(uint64 Bytes) {
        WrittenBytesConsole.fetch_add(Bytes);
    }
    static void AddWrittenBytesFile(uint64 Bytes) {
        WrittenBytesFile.fetch_add(Bytes);
    }
    static void AddWrittenBytesUdp(uint64 Bytes) {
        WrittenBytesUdp.fetch_add(Bytes);
    }
    static void AddWrittenBytesTcp(uint64 Bytes) {
        WrittenBytesTcp.fetch_add(Bytes);
    }
    static void IncSuppressed() {
        SInc(TotalSuppressed);
    }
    static void IncSuppressedByCategory(uint16 CategoryId);

    static SLogMetricsSnapshot Snapshot();

    private:
    static std::atomic<uint64> Enqueued;
    static std::atomic<uint64> DroppedEvicted;
    static std::atomic<uint64> DroppedOverflow;
    static std::atomic<uint64> BlockedEnqueues;
    static std::atomic<uint64> DispatchedBatches;
    static std::atomic<uint64> WrittenBytesConsole;
    static std::atomic<uint64> WrittenBytesFile;
    static std::atomic<uint64> WrittenBytesUdp;
    static std::atomic<uint64> WrittenBytesTcp;
    static std::atomic<uint64> TotalSuppressed;

    template <typename T> static void SInc(std::atomic<T>& Counter) {
        Counter.fetch_add(1);
    }
};
