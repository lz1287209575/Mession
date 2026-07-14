#include "Common/Runtime/Log/LogMetrics.h"

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

SLogMetricsSnapshot MLogMetrics::Snapshot()
{
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
    return S;
}
