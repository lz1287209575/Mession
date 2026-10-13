/**
 * @file ComplexDemo.cpp
 * @brief ComplexDemo 的 await 目标实现(AsyncAdd / AsyncDelayed)。
 */
#include "ComplexDemo.h"

#include <chrono>
#include <cstdio>
#include <thread>

/**
 * @brief AsyncAdd - await 目标(同步):返回已就绪的 A + B。
 */
SFutureResult<int> AsyncAdd(int A, int B) {
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(A + B));
    return SFutureResult<int>(P.GetFuture());
}

/**
 * @brief AsyncDelayed - await 目标(真异步):延迟 50ms 才完成。
 *
 * 起一个 detach 线程 sleep 50ms 后 set value;完成时触发状态机恢复
 * (Frame->Resume),这样才真正走一遍 suspend / resume 路径,而不是 ready 直通。
 *
 * @param V 输入值;结果为 V * 2
 */
SFutureResult<int> AsyncDelayed(int V) {
    auto P = MakeShared<MPromise<TResult<int, FAppError>>>();
    std::thread([P, V]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        P->SetValue(TResult<int, FAppError>::Ok(V * 2)); // 完成 → Then 回调 → Frame->Resume()
    }).detach();
    return SFutureResult<int>(P->GetFuture());
}
