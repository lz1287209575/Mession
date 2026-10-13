/**
 * @file TestLogContext.cpp
 * @brief MLogContext TLS 上下文与 MLogContextScope 作用域测试。
 */
#include "Common/Runtime/Log/LogContext.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"

TEST_CASE(LogContext_SetAndUnset) {
    auto& Ctx = MLogContext::GetTLS();
    // 先清空:同一线程上此前跑过的用例可能留下了 TLS 上下文。
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

TEST_CASE(LogContext_NestedScope) {
    // 同一线程上的 TLS 上下文在各用例间共享;把可能残留的 key 清掉,
    // 断言才能只看到本用例设置的键。
    auto& Ctx = MLogContext::GetTLS();
    Ctx.Unset("key1");
    Ctx.Unset("key2");
    Ctx.Unset("actor");

    {
        MLogContextScope S1("actor", "1001");
        EXPECT_EQ(Ctx.Size(), size_t{1});
    }
    // S1 析构后,"actor" 应当已被解除。
    EXPECT_EQ(Ctx.Size(), size_t{0});
}
