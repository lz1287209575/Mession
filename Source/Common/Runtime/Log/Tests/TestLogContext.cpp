#include "Common/Runtime/Log/Tests/TestHarness.h"
#include "Common/Runtime/Log/LogContext.h"

TEST_CASE(LogContext_SetAndUnset)
{
    auto& Ctx = MLogContext::GetTLS();
    // Ensure clean starting slate across prior tests sharing this TLS.
    Ctx.Unset("key1");
    Ctx.Unset("key2");

    Ctx.Set("key1", "value1");
    Ctx.Set("key2", 42);
    EXPECT_EQ(Ctx.Size(), size_t{2});

    uint32 SnapId = Ctx.CaptureSnapshot();
    EXPECT_TRUE(SnapId < MLogContext::kMaxSnapshots);
    Ctx.ReleaseSnapshot(SnapId);

    Ctx.Unset("key1");
    EXPECT_EQ(Ctx.Size(), size_t{1});
}

TEST_CASE(LogContext_NestedScope)
{
    // TLS context is shared across tests on the same thread; clear any keys
    // we know could be lingering so the assertions see only what this test set.
    auto& Ctx = MLogContext::GetTLS();
    Ctx.Unset("key1");
    Ctx.Unset("key2");
    Ctx.Unset("actor");

    {
        MLogContextScope S1("actor", "1001");
        EXPECT_EQ(Ctx.Size(), size_t{1});
    }
    // S1 destroyed -> "actor" should be unset.
    EXPECT_EQ(Ctx.Size(), size_t{0});
}
