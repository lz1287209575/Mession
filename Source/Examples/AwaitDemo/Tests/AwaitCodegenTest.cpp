// AwaitCodegenTest — await 状态机 codegen 系统化测试（回归保护）
// 覆盖：单 await / 多 await 串行 / 循环 / 真异步 / 并发 / 类成员 /
//       if+early / else await / if 内多 await / else-if 链 / 嵌套 / 3 层嵌套 /
//       分支内循环 / 循环内 if。
// 实现由 MHeaderTool 生成（codegen Impl），本文件只断言结果值。

#include "Common/Runtime/Tests/TestHarness.h"
#include "Examples/AwaitDemo/BranchDemo.h"
#include "Examples/AwaitDemo/ComplexDemo.h"
#include "Examples/AwaitDemo/MemberDemo.h"
#include "Examples/AwaitDemo/StringDemo.h"

#include <chrono>

TEST_CASE(Await_Single_BranchIfAndEarly)
{
    EXPECT_EQ(BranchAsync(5).GetResult().GetValue(), 12);    // if 分支 await：(5+1)*2
    EXPECT_EQ(BranchAsync(-3).GetResult().GetValue(), 0);    // early return
}

TEST_CASE(Await_ElseBranchAwait)
{
    EXPECT_EQ(BranchElseAsync(5).GetResult().GetValue(), 6);   // if 分支：5+1
    EXPECT_EQ(BranchElseAsync(-3).GetResult().GetValue(), 7);  // else 分支：-3+10
}

TEST_CASE(Await_MultiAwaitInIf)
{
    EXPECT_EQ(BranchMultiAwait(5).GetResult().GetValue(), 14);  // A=6 → B=8 → 14
    EXPECT_EQ(BranchMultiAwait(-3).GetResult().GetValue(), 0);
}

TEST_CASE(Await_ElseIfChain)
{
    EXPECT_EQ(BranchChainAsync(20).GetResult().GetValue(), 21);  // 链分支1：20+1
    EXPECT_EQ(BranchChainAsync(5).GetResult().GetValue(), 7);    // 链分支2：5+2
    EXPECT_EQ(BranchChainAsync(-3).GetResult().GetValue(), 0);   // 最后 else：-3+3
}

TEST_CASE(Await_NestedIf)
{
    EXPECT_EQ(NestedIfAsync(20).GetResult().GetValue(), 42);  // 内层 if await：(20+1)*2
    EXPECT_EQ(NestedIfAsync(5).GetResult().GetValue(), 5);    // 内层 early
    EXPECT_EQ(NestedIfAsync(-3).GetResult().GetValue(), 0);   // 外层 early
}

TEST_CASE(Await_DeepNest3Levels)
{
    EXPECT_EQ(DeepNestAsync(200).GetResult().GetValue(), 201);  // 3 层：200+1
    EXPECT_EQ(DeepNestAsync(50).GetResult().GetValue(), 52);    // 2 层：50+2
    EXPECT_EQ(DeepNestAsync(5).GetResult().GetValue(), 5);      // 1 层 early
    EXPECT_EQ(DeepNestAsync(-3).GetResult().GetValue(), 0);     // 外层 early
}

TEST_CASE(Await_BranchLoop)
{
    EXPECT_EQ(BranchLoopAsync(3).GetResult().GetValue(), 4);  // 分支内循环累加
    EXPECT_EQ(BranchLoopAsync(-2).GetResult().GetValue(), 0); // early
}

TEST_CASE(Await_LoopIf)
{
    EXPECT_EQ(LoopIfAsync(4).GetResult().GetValue(), 4);  // 循环内 if（偶数轮累加）
}

TEST_CASE(Await_MultiAwaitSerial)
{
    EXPECT_EQ(ComplexChain(5).GetResult().GetValue(), 44);  // Total=10→11→21→23→44
}

TEST_CASE(Await_LoopAccumulate)
{
    EXPECT_EQ(ComplexLoop(3).GetResult().GetValue(), 4);  // Sum += AsyncAdd2(i,Sum)
}

TEST_CASE(Await_AsyncSuspendResume)
{
    // 真异步：await 挂起 → 50ms 后线程完成 → Frame->Resume 恢复
    const auto T0 = std::chrono::steady_clock::now();
    const int R = ComplexAsync(10).GetResult().GetValue();
    const auto T1 = std::chrono::steady_clock::now();
    const long long Ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(T1 - T0).count();
    EXPECT_EQ(R, 21);           // 10*2+1
    EXPECT_TRUE(Ms >= 40);      // 挂起等待真实发生（非同步 ready）
}

TEST_CASE(Await_DualAsyncSuspend)
{
    EXPECT_EQ(ComplexDualAsync(10).GetResult().GetValue(), 60);  // 两次挂起：20+40
}

TEST_CASE(Await_ConcurrentAsync)
{
    // 并发：两个异步同时挂起 → 并行完成（总耗时 ≈ 单次）
    const auto T0 = std::chrono::steady_clock::now();
    auto F1 = ComplexAsync(10);
    auto F2 = ComplexAsync(20);
    const int A1 = F1.GetResult().GetValue();
    const int A2 = F2.GetResult().GetValue();
    const auto T1 = std::chrono::steady_clock::now();
    const long long Ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(T1 - T0).count();
    EXPECT_EQ(A1, 21);
    EXPECT_EQ(A2, 41);
    EXPECT_TRUE(Ms < 90);  // 并行 ≈ 50ms（非串行 100ms）
}

TEST_CASE(Await_ClassMemberAsync)
{
    MemberService Service;
    EXPECT_EQ(Service.MemberAsync(5).GetResult().GetValue(), 11);  // 5*2+1
}

TEST_CASE(Await_NonIntReturnType)
{
    // 非 int 返回类型（MString）：泛型 R 验证
    EXPECT_TRUE(StringAsync(7).GetResult().GetValue() == "v=7!");
}
