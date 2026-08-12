// AwaitDemo — await 状态机框架端到端验证（C# async 模型）
// AwaitDemoCompute 的实现由 codegen 生成（含业务逻辑段），本文件只提供
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

int main()
{
    // 业务逻辑：Mid = 5*3 = 15 → await Helper(15) = 30 → 返回 30+1 = 31
    const int R = AwaitDemoCompute(5).GetResult().GetValue();
    std::printf("AwaitDemo: compute=%d\n", R);
    return R == 31 ? 0 : 1;
}
