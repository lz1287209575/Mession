// AwaitCodegenTest main — 汇总运行（测试 TU 合并共享计数）
#include "AwaitCodegenTest.cpp"

int main()
{
    std::printf("[ Await_Single_BranchIfAndEarly ]\n");
    Test_Await_Single_BranchIfAndEarly();
    std::printf("[ Await_ElseBranchAwait ]\n");
    Test_Await_ElseBranchAwait();
    std::printf("[ Await_MultiAwaitInIf ]\n");
    Test_Await_MultiAwaitInIf();
    std::printf("[ Await_ElseIfChain ]\n");
    Test_Await_ElseIfChain();
    std::printf("[ Await_NestedIf ]\n");
    Test_Await_NestedIf();
    std::printf("[ Await_DeepNest3Levels ]\n");
    Test_Await_DeepNest3Levels();
    std::printf("[ Await_BranchLoop ]\n");
    Test_Await_BranchLoop();
    std::printf("[ Await_LoopIf ]\n");
    Test_Await_LoopIf();
    std::printf("[ Await_MultiAwaitSerial ]\n");
    Test_Await_MultiAwaitSerial();
    std::printf("[ Await_LoopAccumulate ]\n");
    Test_Await_LoopAccumulate();
    std::printf("[ Await_AsyncSuspendResume ]\n");
    Test_Await_AsyncSuspendResume();
    std::printf("[ Await_DualAsyncSuspend ]\n");
    Test_Await_DualAsyncSuspend();
    std::printf("[ Await_ConcurrentAsync ]\n");
    Test_Await_ConcurrentAsync();
    std::printf("[ Await_ClassMemberAsync ]\n");
    Test_Await_ClassMemberAsync();
    std::printf("[ Await_MemberMethodTarget ]\n");
    Test_Await_MemberMethodTarget();
    std::printf("[ Await_FallThrough ]\n");
    Test_Await_FallThrough();
    std::printf("[ Await_NonIntReturnType ]\n");
    Test_Await_NonIntReturnType();
    RUN_TESTS();
}
