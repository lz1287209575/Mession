// AwaitDemo — await 状态机框架端到端验证（C# async 模型，方案 B）
// 业务逻辑体（含 await 表达式）在 .cpp（#ifdef 保护——codegen 输入）；
// 编译版实现由 MHeaderTool 生成（独立编译单元），业务编译（宏关）只提供
// await 目标实现 + main 调用（真实场景由 ServerCall/RPC 自动分发）。

#include "AwaitDemo.h"

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
    int R = TAwaitable<decltype(&AwaitDemoHelper), int, int>(Mid);  // await 点
    return SFutureResult<int>(TResult<int, FAppError>::Ok(R + 1));  // 正常业务逻辑 2
}
#endif

int main()
{
    // 业务逻辑：Mid = 5*3 = 15 → await Helper(15) = 30 → 返回 30+1 = 31
    const int R = AwaitDemoCompute(5).GetResult().GetValue();
    std::printf("AwaitDemo: compute=%d\n", R);
    return R == 31 ? 0 : 1;
}
