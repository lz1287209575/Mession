#pragma once

#include "Common/Runtime/Async/MAsync.h"

#include <tuple>
#include <type_traits>

/**
 * TAwaitable — P5 awaitable 类型层（KD-2 / KD-3, spec 2026-07-30-async-p5）
 *
 * 业务侧唯一允许的两种 await 表达式形态（KD-7）：
 *   形式 A：TAwaitable<F, R>(args...)   — F 是返回 SFutureResult<R> 的函数，
 *           args... 是调用参数（本类型存储，§4.5 业务不可见）
 *   形式 B：TAwaitable<SFutureResult<R>, R>() — await 一个已存在的 future 变量；
 *           无参构造，AsAwaiter() 由 P5 codegen 注入（KD-6：业务侧不持有
 *           变量引用，codegen 通过 AST 作用域分析绑定实际 future）
 *
 * R 推导（KD-3）：业务侧永远写完整三参 / 两参，不让 CTAD 推中间形参
 * （CTAD 不能从函数指针返回类型推中间模板参数）。
 */
template <typename F, typename R, typename... Args>
class TAwaitable
{
public:
    // 形式 A — 函数调用：存储调用参数（§4.5 业务不可见元素）。
    // C++17（无 requires 子句）：enable_if 与形式 B 无参构造互斥
    // （F 非 SFutureResult<R> 或有参数 → 形式 A）。
    template <typename F2 = F,
              std::enable_if_t<(!std::is_same_v<F2, SFutureResult<R>> || sizeof...(Args) > 0), int> = 0>
    explicit TAwaitable(Args... InArgs)
        : StoredArgs(std::move(InArgs)...)
    {
    }

    // 形式 B — future 变量：无参构造；仅当 F 是 SFutureResult<R> 时可用
    template <typename F2 = F,
              std::enable_if_t<std::is_same_v<F2, SFutureResult<R>>, int> = 0>
    explicit TAwaitable()
    {
    }

    // AsAwaiter() 由 P5 codegen 注入（KD-2 §4.1 / KD-6）：类型层只提供构造与
    // 存储——TAwaitable 不持有 F 的运行时实例（F 是模板类型参数，业务侧写
    // 完整三参/两参），codegen 在生成的状态机里直接调用真实函数名 / 绑定
    // 实际 future 变量。此处占位，防止误用（业务侧 await 表达式在类型层
    // 单独使用无意义，必须经 codegen 转换）。
    auto AsAwaiter()
    {
        static_assert(std::is_void_v<F>,
            "TAwaitable::AsAwaiter() 由 P5 codegen 注入（KD-2 §4.1 / KD-6）；"
            "类型层只提供构造与 StoredArgs 存储，运行时 await 由 codegen 生成的状态机驱动");
    }

private:
    std::tuple<Args...> StoredArgs;
};
