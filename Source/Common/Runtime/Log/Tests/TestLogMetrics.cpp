/**
 * @file TestLogMetrics.cpp
 * @brief MLogMetrics 计数器与快照一致性测试。
 */
#include "Common/Runtime/Log/LogMetrics.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"

TEST_CASE(LogMetrics_CountersAccumulate) {
    // 先取基线:本 TU 内其他用例可能已经动过这些计数器。
    SLogMetricsSnapshot Before              = MLogMetrics::Snapshot();
    const uint64        BaseEnqueued        = Before.Enqueued;
    const uint64        BaseDroppedEvicted  = Before.DroppedEvicted;
    const uint64        BaseDroppedOverflow = Before.DroppedOverflow;
    const uint64        BaseBlocked         = Before.BlockedEnqueues;
    const uint64        BaseDispatched      = Before.DispatchedBatches;
    const uint64        BaseConsole         = Before.WrittenBytesConsole;
    const uint64        BaseFile            = Before.WrittenBytesFile;
    const uint64        BaseSuppressed      = Before.TotalSuppressed;

    MLogMetrics::IncEnqueued();
    MLogMetrics::IncEnqueued();
    MLogMetrics::IncEnqueued();

    MLogMetrics::IncDroppedEvicted();
    MLogMetrics::IncDroppedOverflow();
    MLogMetrics::IncDroppedOverflow();

    MLogMetrics::IncBlockedEnqueues();
    MLogMetrics::IncDispatchedBatches();
    MLogMetrics::IncDispatchedBatches();

    MLogMetrics::AddWrittenBytesConsole(128);
    MLogMetrics::AddWrittenBytesConsole(64);

    MLogMetrics::AddWrittenBytesFile(512);

    MLogMetrics::IncSuppressed();

    SLogMetricsSnapshot After = MLogMetrics::Snapshot();
    EXPECT_EQ(After.Enqueued, BaseEnqueued + 3);
    EXPECT_EQ(After.DroppedEvicted, BaseDroppedEvicted + 1);
    EXPECT_EQ(After.DroppedOverflow, BaseDroppedOverflow + 2);
    EXPECT_EQ(After.BlockedEnqueues, BaseBlocked + 1);
    EXPECT_EQ(After.DispatchedBatches, BaseDispatched + 2);
    EXPECT_EQ(After.WrittenBytesConsole, BaseConsole + 192);
    EXPECT_EQ(After.WrittenBytesFile, BaseFile + 512);
    EXPECT_EQ(After.TotalSuppressed, BaseSuppressed + 1);
}

TEST_CASE(LogMetrics_SnapshotIsConsistent) {
    // 多次自增之后,快照应当反映累计状态。每次调用写入一个已知增量,
    // 再重新取快照确认。
    SLogMetricsSnapshot S0 = MLogMetrics::Snapshot();

    for (int i = 0; i < 1000; ++i) {
        MLogMetrics::IncEnqueued();
    }
    MLogMetrics::AddWrittenBytesFile(4096);
    MLogMetrics::IncSuppressed();

    SLogMetricsSnapshot S1 = MLogMetrics::Snapshot();
    EXPECT_EQ(S1.Enqueued - S0.Enqueued, uint64{1000});
    EXPECT_EQ(S1.WrittenBytesFile - S0.WrittenBytesFile, uint64{4096});
    EXPECT_EQ(S1.TotalSuppressed - S0.TotalSuppressed, uint64{1});
    // 未触碰过的计数器必须保持不变。
    EXPECT_EQ(S1.DroppedEvicted, S0.DroppedEvicted);
    EXPECT_EQ(S1.DroppedOverflow, S0.DroppedOverflow);
    EXPECT_EQ(S1.BlockedEnqueues, S0.BlockedEnqueues);
    EXPECT_EQ(S1.DispatchedBatches, S0.DispatchedBatches);
    EXPECT_EQ(S1.WrittenBytesConsole, S0.WrittenBytesConsole);
    EXPECT_EQ(S1.WrittenBytesUdp, S0.WrittenBytesUdp);
    EXPECT_EQ(S1.WrittenBytesTcp, S0.WrittenBytesTcp);
}
