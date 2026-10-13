/**
 * @file AsyncDemoImpl.cpp
 * @brief AsyncDemo 的实现(不含 main),抽出来给测试链接用。
 *
 * 只放 ComputeAsync / ChainAsync 的实现。测试 target 有自己的 main,若把
 * AsyncDemo.cpp 整个链进去会撞 main 符号,所以单独拆出这个 TU。
 */
#include "AsyncDemo.h"

#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"

namespace AsyncDemoNS {

    SFutureResult<int> ComputeAsync(int Seed) {
        // 局部变量名 Frame 是 AWAIT_OK 的隐式约定:AwaitMacros.h 里该宏展开为
        // `Frame->AwaitOk(expr)`。
        auto Frame = MakeShared<MHeaderTool_AsyncFrame_Free_ComputeAsync>();

        // Ready-Ok 路径:构造一个已就绪的 future,携带 Seed*2。
        MPromise<TResult<int, FAppError>> Promise;
        Promise.SetValue(TResult<int, FAppError>::Ok(Seed * 2));

        // AWAIT_OK 原样返回被 await 的 future(P4 v1 的 AwaitOk 是直通实现),
        // 因此这里的返回类型与 SFutureResult<int> 匹配。
        return AWAIT_OK(SFutureResult<int>(Promise.GetFuture()));
    }

    SFutureResult<int> ChainAsync(int Seed) {
        auto Frame = MakeShared<MHeaderTool_AsyncFrame_Free_ChainAsync>();

        // await 内层 future。P4 v1 是单次 await 的直通实现:被 await 的 future 存进
        // AwaitedSlot,已就绪的值另存 StoredValue;因此当它本来就就绪时,可以在
        // 这里同步读到结果。
        SFutureResult<int> InnerReturned = AWAIT_OK(ComputeAsync(Seed));
        const int          Inner         = InnerReturned.PeekResult().GetValue();

        // 派生外层结果:Inner + 1,就绪即 OK。
        MPromise<TResult<int, FAppError>> Promise;
        Promise.SetValue(TResult<int, FAppError>::Ok(Inner + 1));

        return AWAIT_OK(SFutureResult<int>(Promise.GetFuture()));
    }

} // namespace AsyncDemoNS