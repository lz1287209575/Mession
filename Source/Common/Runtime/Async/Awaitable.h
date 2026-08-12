#pragma once

#include "Common/Runtime/Async/MAsync.h"

#include <type_traits>

// 从函数指针类型提取返回类型（F 是 auto 非类型模板参数——await 目标函数）
template <typename T>
struct TAwaitableFnTraits;
template <typename R, typename... Args>
struct TAwaitableFnTraits<R (*)(Args...)>
{
    using Ret = R;
};

/**
 * TAwaitable — P5 awaitable 类型层（最简签名）
 *
 * 业务侧唯一写法：TAwaitable<F>(args...)
 *   F    — await 目标函数名（auto 非类型模板参数，如 TAwaitable<AsyncAdd>(a,b)）
 *   args — 调用参数（构造函数推导，不存储）
 *   R    — await 返回类型，从 F 的函数指针返回类型（SFutureResult<R>）提取——
 *          业务侧不需要写 R / Args。
 *
 * 占位转换（operator ResultType / operator SFutureResult<ResultType>）：
 * 业务函数体（#ifdef 保护，codegen 解析时可见）里的 `int R =
 * TAwaitable<F>(args)` 与 `return TAwaitable<F>(args)` 需要能编译——这两个
 * 转换保证类型匹配。运行时由 codegen 生成的含业务逻辑的状态机实现覆盖函数
 * 定义（业务编译时 #ifdef 体不可见，占位转换不参与运行）。
 */
template <auto F>
class TAwaitable
{
public:
    template <typename... Args>
    explicit TAwaitable(Args&&...) {}

    using RetType = typename TAwaitableFnTraits<decltype(F)>::Ret;  // SFutureResult<R>
    using ResultType = typename RetType::InnerType;                 // R

    // 占位：await 结果赋值（`int R = TAwaitable<F>(args);`）
    operator ResultType() const
    {
        return ResultType{};
    }

    // 占位：直接返回（`return TAwaitable<F>(args);`）
    operator RetType() const
    {
        MPromise<TResult<ResultType, FAppError>> P;
        P.SetValue(TResult<ResultType, FAppError>::Err(FAppError{
            "await_not_wired",
            "TAwaitable 运行时需经 codegen 生成的状态机驱动实现"}));
        return RetType(P.GetFuture());
    }

    // AsAwaiter() 由 await 状态机 codegen 注入——类型层只提供构造与占位转换
    auto AsAwaiter()
    {
        static_assert(std::is_void_v<decltype(F)>,
            "TAwaitable::AsAwaiter() 由 await 状态机 codegen 注入");
    }
};
