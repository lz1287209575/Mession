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

    // 真正异步：await 挂起（非 ready）→ 50ms 后线程完成 → Frame->Resume 恢复 → 结果
    std::printf("[ComplexAsync] 调用（await 将挂起）...\n");
    const auto T0 = std::chrono::steady_clock::now();
    const int A = ComplexAsync(10).GetResult().GetValue();
    const auto T1 = std::chrono::steady_clock::now();
    const long long ElapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(T1 - T0).count();
    std::printf("[ComplexAsync] 恢复完成: 10*2+1 = %d（耗时 %lldms——挂起等待真实发生）\n",
        A, ElapsedMs);

    return (R == 31 && C == 44 && L == 4 && A == 21 && ElapsedMs >= 40) ? 0 : 1;
}
