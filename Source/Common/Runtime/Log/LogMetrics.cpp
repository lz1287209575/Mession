/**
 * @file LogMetrics.cpp
 * @brief MLogMetrics 实现 —— 全局与按分类的日志计数器。
 */
#include "Common/Runtime/Log/LogMetrics.h"
#include "Common/Runtime/Log/LogRegistry.h"
#include <mutex>

std::atomic<uint64> MLogMetrics::Enqueued{0};
std::atomic<uint64> MLogMetrics::DroppedEvicted{0};
std::atomic<uint64> MLogMetrics::DroppedOverflow{0};
std::atomic<uint64> MLogMetrics::BlockedEnqueues{0};
std::atomic<uint64> MLogMetrics::DispatchedBatches{0};
std::atomic<uint64> MLogMetrics::WrittenBytesConsole{0};
std::atomic<uint64> MLogMetrics::WrittenBytesFile{0};
std::atomic<uint64> MLogMetrics::WrittenBytesUdp{0};
std::atomic<uint64> MLogMetrics::WrittenBytesTcp{0};
std::atomic<uint64> MLogMetrics::TotalSuppressed{0};

// 按分类的抑制计数器放在一个受互斥量保护的小 vector 里,首次
// IncSuppressedByCategory 调用时惰性扩容,下标即 SLogCategory::Id。
static TVector<uint64> GSuppressedByCategory;
static std::mutex      GSuppressedByCategoryMutex;

void MLogMetrics::IncSuppressedByCategory(uint16 CategoryId) {
    std::lock_guard<std::mutex> L(GSuppressedByCategoryMutex);
    if (GSuppressedByCategory.size() <= CategoryId) {
        GSuppressedByCategory.resize(static_cast<size_t>(CategoryId) + 1, 0);
    }
    GSuppressedByCategory[CategoryId] += 1;
}

SLogMetricsSnapshot MLogMetrics::Snapshot() {
    SLogMetricsSnapshot S;
    S.Enqueued            = Enqueued.load();
    S.DroppedEvicted      = DroppedEvicted.load();
    S.DroppedOverflow     = DroppedOverflow.load();
    S.BlockedEnqueues     = BlockedEnqueues.load();
    S.DispatchedBatches   = DispatchedBatches.load();
    S.WrittenBytesConsole = WrittenBytesConsole.load();
    S.WrittenBytesFile    = WrittenBytesFile.load();
    S.WrittenBytesUdp     = WrittenBytesUdp.load();
    S.WrittenBytesTcp     = WrittenBytesTcp.load();
    S.TotalSuppressed     = TotalSuppressed.load();

    {
        std::lock_guard<std::mutex> L(GSuppressedByCategoryMutex);
        S.SuppressedByCategory = GSuppressedByCategory;
    }

    return S;
}