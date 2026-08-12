// AwaitDemo — await 状态机框架端到端验证（调用 codegen 生成的驱动实现）

#include "AwaitDemo.h"
#include "AwaitDemo_FreeAwaitImpl.mgenerated.cpp"

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
    // 单 await：AwaitDemoHelper(5) = 10
    const int R = AwaitDemoCompute(5).GetResult().GetValue();
    std::printf("AwaitDemo: compute=%d\n", R);
    return R == 10 ? 0 : 1;
}
