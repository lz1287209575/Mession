#include "Common/Runtime/Log/Tests/TestHarness.h"
#include "Common/Runtime/Log/LogMetrics.h"

TEST_CASE(LogMetrics_CountersAccumulate)
{
    // Snapshot baseline; other tests in this TU may have touched these counters.
    SLogMetricsSnapshot Before = MLogMetrics::Snapshot();
    const uint64 BaseEnqueued = Before.Enqueued;
    const uint64 BaseDroppedEvicted = Before.DroppedEvicted;
    const uint64 BaseDroppedOverflow = Before.DroppedOverflow;
    const uint64 BaseBlocked = Before.BlockedEnqueues;
    const uint64 BaseDispatched = Before.DispatchedBatches;
    const uint64 BaseConsole = Before.WrittenBytesConsole;
    const uint64 BaseFile = Before.WrittenBytesFile;
    const uint64 BaseSuppressed = Before.TotalSuppressed;

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

TEST_CASE(LogMetrics_SnapshotIsConsistent)
{
    // After many increments, the snapshot should reflect the cumulative state.
    // Each call writes a known delta and then re-snapshots to confirm.
    SLogMetricsSnapshot S0 = MLogMetrics::Snapshot();

    for (int i = 0; i < 1000; ++i)
    {
        MLogMetrics::IncEnqueued();
    }
    MLogMetrics::AddWrittenBytesFile(4096);
    MLogMetrics::IncSuppressed();

    SLogMetricsSnapshot S1 = MLogMetrics::Snapshot();
    EXPECT_EQ(S1.Enqueued - S0.Enqueued, uint64{1000});
    EXPECT_EQ(S1.WrittenBytesFile - S0.WrittenBytesFile, uint64{4096});
    EXPECT_EQ(S1.TotalSuppressed - S0.TotalSuppressed, uint64{1});
    // Counters we did not touch must remain unchanged.
    EXPECT_EQ(S1.DroppedEvicted, S0.DroppedEvicted);
    EXPECT_EQ(S1.DroppedOverflow, S0.DroppedOverflow);
    EXPECT_EQ(S1.BlockedEnqueues, S0.BlockedEnqueues);
    EXPECT_EQ(S1.DispatchedBatches, S0.DispatchedBatches);
    EXPECT_EQ(S1.WrittenBytesConsole, S0.WrittenBytesConsole);
    EXPECT_EQ(S1.WrittenBytesUdp, S0.WrittenBytesUdp);
    EXPECT_EQ(S1.WrittenBytesTcp, S0.WrittenBytesTcp);
}
