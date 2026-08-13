// AwaitDemo — await 状态机框架端到端验证（C# async 模型，方案 B）
// 业务逻辑体（含 await 表达式）在 .cpp（#ifdef 保护——codegen 输入）；
// 编译版实现由 MHeaderTool 生成（独立编译单元），业务编译（宏关）只提供
// await 目标实现 + main 调用（真实场景由 ServerCall/RPC 自动分发）。

#include "AwaitDemo.h"
#include "ComplexDemo.h"
#include "MemberDemo.h"
#include "StringDemo.h"
#include "BranchDemo.h"

#include <chrono>
#include <cstdio>

// await 目标实现（模拟异步 I/O：ready future，V*2）
SFutureResult<int> AwaitDemoHelper(int V)
{
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(V * 2));
    return SFutureResult<int>(P.GetFuture());
}

// MFUNCTION(Async) 业务逻辑体（codegen 输入：正常业务逻辑 + await 点）
#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(Async)
SFutureResult<int> AwaitDemoCompute(int Seed)
{
    int Mid = Seed * 3;                                             // 正常业务逻辑 1
    int R = TAwaitable<AwaitDemoHelper>(Mid);  // await 点
    return SFutureResult<int>(TResult<int, FAppError>::Ok(R + 1));  // 正常业务逻辑 2
}
#endif

int main()
{
    // 单 await：Mid = 5*3 = 15 → await Helper(15) = 30 → 返回 30+1 = 31
    const int R = AwaitDemoCompute(5).GetResult().GetValue();
    std::printf("AwaitDemo:     compute=%d\n", R);

    // 多 await 串行：Total=10 → await1(10,1)=11 → Total=21 → await2(21,2)=23 → 21+23=44
    const int C = ComplexChain(5).GetResult().GetValue();
    std::printf("ComplexChain(5): %d\n", C);

    // 循环 await：Sum += AsyncAdd(i, Sum)：i=0 → +0 → 0；i=1 → +1 → 1；i=2 → +3 → 4
    const int L = ComplexLoop(3).GetResult().GetValue();
    std::printf("ComplexLoop(3):  %d\n", L);

    // ① 函数内多次挂起/恢复（两个异步 await 串行：~100ms，状态机切换两次）
    {
        const auto T0 = std::chrono::steady_clock::now();
        const int D = ComplexDualAsync(10).GetResult().GetValue();
        const auto T1 = std::chrono::steady_clock::now();
        const long long Ms = std::chrono::duration_cast<std::chrono::milliseconds>(T1 - T0).count();
        std::printf("[DualAsync] 两次挂起/恢复: 20+40 = %d（耗时 %lldms ≈ 2×50ms）\n", D, Ms);
    }

    // ② 并发切换：两个异步任务同时挂起（不阻塞）→ 并行完成
    //    总耗时 ≈ 50ms（而非串行 100ms）——证明 await 不卡调用方（真正异步切换）
    {
        const auto T0 = std::chrono::steady_clock::now();
        auto F1 = ComplexAsync(10);          // 挂起，立即返回 future（不阻塞）
        auto F2 = ComplexAsync(20);          // 第二个也挂起（两个异步并行）
        std::printf("[Concurrent] 两个异步已挂起（不阻塞），主线程继续做事...\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 模拟主线程做别的事
        const int A1 = F1.GetResult().GetValue();   // 等待（此时两个都快完成）
        const int A2 = F2.GetResult().GetValue();
        const auto T1 = std::chrono::steady_clock::now();
        const long long Ms = std::chrono::duration_cast<std::chrono::milliseconds>(T1 - T0).count();
        std::printf("[Concurrent] F1=%d F2=%d 总耗时 %lldms（≈50ms 而非 100ms → 并发异步切换）\n",
            A1, A2, Ms);
    }

    // MCLASS 类成员 async：MemberService::MemberAsync(5) → await RemoteFetch(5)=10 → 11
    MemberService Service;
    const int M = Service.MemberAsync(5).GetResult().GetValue();
    std::printf("MemberAsync(5):  %d\n", M);

    // 分支控制流：N>0 → if 内 await（5+1=6 → *2=12）；N<=0 → early return 0
    const int B1 = BranchAsync(5).GetResult().GetValue();
    const int B2 = BranchAsync(-3).GetResult().GetValue();
    std::printf("BranchAsync(5):  %d（if 分支 await）\n", B1);
    std::printf("BranchAsync(-3): %d（early return）\n", B2);

    // if/else 两边都 await：N>0 → if(5+1=6)；N<=0 → else(N+10=7)
    const int E1 = BranchElseAsync(5).GetResult().GetValue();
    const int E2 = BranchElseAsync(-3).GetResult().GetValue();
    std::printf("BranchElseAsync(5):  %d（if 分支 await）\n", E1);
    std::printf("BranchElseAsync(-3): %d（else 分支 await）\n", E2);

    // if 分支内 2 await 串行：A=N+1 → B=A+2 → A+B；N<=0 → 0
    const int M1 = BranchMultiAwait(5).GetResult().GetValue();
    const int M2 = BranchMultiAwait(-3).GetResult().GetValue();
    std::printf("BranchMultiAwait(5):  %d（if 内 2 await 串行）\n", M1);
    std::printf("BranchMultiAwait(-3): %d（early return）\n", M2);

    // else-if 链（三分支都 await）：N>10 → N+1；N>0 → N+2；else → N+3
    const int CH1 = BranchChainAsync(20).GetResult().GetValue();
    const int CH2 = BranchChainAsync(5).GetResult().GetValue();
    const int CH3 = BranchChainAsync(-3).GetResult().GetValue();
    std::printf("BranchChainAsync(20): %d（链分支1）\n", CH1);
    std::printf("BranchChainAsync(5):  %d（链分支2）\n", CH2);
    std::printf("BranchChainAsync(-3): %d（最后 else）\n", CH3);

    // 嵌套 if：N>0 → 内层 N>10 → await(N+1)*2；内层 early 5；外层 early 0
    const int N1 = NestedIfAsync(20).GetResult().GetValue();
    const int N2 = NestedIfAsync(5).GetResult().GetValue();
    const int N3 = NestedIfAsync(-3).GetResult().GetValue();
    std::printf("NestedIfAsync(20): %d（内层 if await）\n", N1);
    std::printf("NestedIfAsync(5):  %d（内层 early）\n", N2);
    std::printf("NestedIfAsync(-3): %d（外层 early）\n", N3);

    // 分支内循环：N>0 → 循环 Sum += AsyncAdd2(i,Sum)（0→0→1→4）；N<=0 → 0
    const int BL1 = BranchLoopAsync(3).GetResult().GetValue();
    const int BL2 = BranchLoopAsync(-2).GetResult().GetValue();
    std::printf("BranchLoopAsync(3):  %d（分支内循环 await）\n", BL1);
    std::printf("BranchLoopAsync(-2): %d（early）\n", BL2);

    // 3 层嵌套（通用递归）：200→3层await(201)；50→2层await(52)；5→1层early(5)；-3→0
    const int D1 = DeepNestAsync(200).GetResult().GetValue();
    const int D2 = DeepNestAsync(50).GetResult().GetValue();
    const int D3 = DeepNestAsync(5).GetResult().GetValue();
    const int D4 = DeepNestAsync(-3).GetResult().GetValue();
    std::printf("DeepNestAsync(200): %d（3 层嵌套 await）\n", D1);
    std::printf("DeepNestAsync(50):  %d（2 层嵌套 await）\n", D2);
    std::printf("DeepNestAsync(5):   %d（1 层 early）\n", D3);
    std::printf("DeepNestAsync(-3):  %d（外层 early）\n", D4);

    // 循环内 if：i 偶数轮累加（i+1）：0→+1→1；2→+3→4 → Sum=4
    const int LI = LoopIfAsync(4).GetResult().GetValue();
    std::printf("LoopIfAsync(4):     %d（循环内 if await）\n", LI);

    // 非 int 返回类型（MString）：StringFetch(7) = "v=7" → +"!" = "v=7!"
    const MString S1 = StringAsync(7).GetResult().GetValue();
    std::printf("StringAsync(7):  %s（MString 返回类型）\n", S1.c_str());

    return (R == 31 && C == 44 && L == 4 && M == 11 && B1 == 12 && B2 == 0
        && E1 == 6 && E2 == 7 && M1 == 14 && M2 == 0
        && CH1 == 21 && CH2 == 7 && CH3 == 0
        && N1 == 42 && N2 == 5 && N3 == 0
        && BL1 == 4 && BL2 == 0
        && D1 == 201 && D2 == 52 && D3 == 5 && D4 == 0
        && LI == 4 && S1 == "v=7!") ? 0 : 1;
}
