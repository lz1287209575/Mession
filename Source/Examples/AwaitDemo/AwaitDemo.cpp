// AwaitDemo — await 状态机框架端到端验证（C# async 模型，方案 B）
// 业务逻辑体（含 await 表达式）在 .cpp（#ifdef 保护——codegen 输入）；
// 编译版实现由 MHeaderTool 生成（独立编译单元），业务编译（宏关）只提供
// await 目标实现 + main 调用（真实场景由 ServerCall/RPC 自动分发）。

#include "AwaitDemo.h"
#include "ComplexDemo.h"

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

    return (R == 31 && C == 44 && L == 4) ? 0 : 1;
}
