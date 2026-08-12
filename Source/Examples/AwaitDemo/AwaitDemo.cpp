// AwaitDemo — await 状态机框架端到端验证
// MFUNCTION(Async) 函数实现由业务写（用 codegen 生成的状态机 Frame 驱动）。

#include "AwaitDemo.h"
#include "AwaitDemo_FreeAwaitStateMachine.h"  // codegen 生成的 Frame（.h 辅助物）

#include <cstdio>

// await 目标实现（模拟异步 I/O：ready future，V*2）
SFutureResult<int> AwaitDemoHelper(int V)
{
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(V * 2));
    return SFutureResult<int>(P.GetFuture());
}

// MFUNCTION(Async) 业务实现——业务自己写，用生成的状态机 Frame 驱动
SFutureResult<int> AwaitDemoCompute(int Seed)
{
    auto Frame = MakeShared<MHeaderTool_AwaitFrame_Free_AwaitDemoCompute>();
    Frame->Seed = Seed;
    Frame->Start();
    return Frame->GetFuture();
}

int main()
{
    // 单 await：AwaitDemoHelper(5) = 10
    const int R = AwaitDemoCompute(5).GetResult().GetValue();
    std::printf("AwaitDemo: compute=%d\n", R);
    return R == 10 ? 0 : 1;
}
