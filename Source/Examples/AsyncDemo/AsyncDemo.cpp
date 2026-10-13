/**
 * @file AsyncDemo.cpp
 * @brief AsyncDemo - C++17 async/await 端到端示例的实现(含 main)。
 *
 * 两个自由 async 函数 + 一个 main():依次跑一次内层 future 的 await、一次 await
 * 链、一次同步屏障(Wait())。成功时打印 `AsyncDemo: result = 43`。
 *
 * @note 不依赖事件循环,也不依赖服务注册 —— 就是个跑在主线程上的普通可执行文件。
 */
#include "AsyncDemo.h"

#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"

#include <cstdio>

namespace AsyncDemoNS {

    /**
     * @brief ComputeAsync - 内层 async,返回 Seed * 2(同步 ready)。
     */
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

/**
 * @brief main - 驱动一次 await 链并校验结果是 43。
 * @return 0 成功;1 结果不符;2 future 失败。
 */
int main() {
    auto F = AsyncDemoNS::ChainAsync(21);

    // 同步屏障,处在任何 Async 函数之外。上面的 future 都是 ready-Ok,所以这里
    // 会立即完成;不依赖事件循环线程,因而不会触发 MAsyncContext::IsSameContext 的
    // 跨上下文红线。
    F.Wait();

    if (!F.IsOk()) {
        // FAppError 只有 Code / Message 两个 MString 字段,没有 What() 成员,
        // 所以这里取 Message。
        std::printf("AsyncDemo: failed (%s)\n", F.GetError().Message.c_str());
        return 2;
    }

    const int Value = F.GetResult().GetValue();
    std::printf("AsyncDemo: result = %d\n", Value);
    return (Value == 43) ? 0 : 1;
}