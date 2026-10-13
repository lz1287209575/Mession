/**
 * @file Result.h
 * @brief TResult / MakeErrorResult —— 无异常风格的统一返回值(值 + 错误二选一)。
 *
 * @detail 约定用静态工厂构造:Ok(...) 或 Err(...)。默认构造出来的是"既非成功
 *   也非失败"的空壳(两个 TOptional 都为空),IsOk() 与 IsErr() 同时为 false,
 *   因此不要先默认构造再手工赋值字段。
 */
#pragma once

#include "Common/Runtime/MLib.h"

/**
 * @brief TResult<T, E> - 成功时带值、失败时带错误的返回类型。
 * @tparam T 成功值类型
 * @tparam E 错误类型,默认 MString
 *
 * @detail Value/Error 互斥(只用其中一个),getter 不做检查:GetValue() 前必须先
 *   IsOk(),GetError() 前必须先 IsErr(),否则解引用空 TOptional 是未定义行为。
 */
template <typename T, typename E = MString> struct TResult {
    TOptional<T> Value;
    TOptional<E> Error;

    /** @brief 构造成功结果(只填 Value)。 */
    static TResult Ok(T InValue) {
        TResult R;
        R.Value = InValue;
        return R;
    }

    /** @brief 构造失败结果(只填 Error)。 */
    static TResult Err(E InError) {
        TResult R;
        R.Error = InError;
        return R;
    }

    /** @brief 是否成功(以 Value 是否存在判定)。 */
    bool IsOk() const {
        return Value.has_value();
    }
    /** @brief 是否失败(以 Error 是否存在判定)。 */
    bool IsErr() const {
        return Error.has_value();
    }
    /** @brief 取成功值;调用前须 IsOk()。 */
    T& GetValue() {
        return *Value;
    }
    /** @brief 取成功值(只读);调用前须 IsOk()。 */
    const T& GetValue() const {
        return *Value;
    }
    /** @brief 取错误;调用前须 IsErr()。 */
    E& GetError() {
        return *Error;
    }
    /** @brief 取错误(只读);调用前须 IsErr()。 */
    const E& GetError() const {
        return *Error;
    }
};

/**
 * @brief TResult<void, E> 特化 - 只表达成功/失败,不带值。
 * @tparam E 错误类型,默认 MString
 *
 * @detail 用 bSuccess 而非 TOptional<void> 表示成功;Error 仅在失败时填充。
 *   同样地,默认构造的结果 IsOk()/IsErr() 都是 false。
 */
template <typename E> struct TResult<void, E> {
    bool         bSuccess = false;
    TOptional<E> Error;

    /** @brief 构造成功结果(bSuccess=true,Error 不填)。 */
    static TResult Ok() {
        TResult R;
        R.bSuccess = true;
        return R;
    }

    /** @brief 构造失败结果(bSuccess=false 并填 Error)。 */
    static TResult Err(E InError) {
        TResult R;
        R.bSuccess = false;
        R.Error    = InError;
        return R;
    }

    /** @brief 是否成功。 */
    bool IsOk() const {
        return bSuccess;
    }
    /** @brief 是否失败(以 Error 是否存在判定,而非 bSuccess)。 */
    bool IsErr() const {
        return Error.has_value();
    }
    /** @brief 取错误;调用前须 IsErr()。 */
    E& GetError() {
        return *Error;
    }
    /** @brief 取错误(只读);调用前须 IsErr()。 */
    const E& GetError() const {
        return *Error;
    }
};

/**
 * @brief MakeErrorResult - 由错误对象直接构造失败结果(省去写模板参数)。
 * @param Error 错误内容,按值前进
 * @return TResult<TResponse, TError>::Err(Error)
 */
template <typename TResponse, typename TError> inline TResult<TResponse, TError> MakeErrorResult(TError Error) {
    return TResult<TResponse, TError>::Err(std::move(Error));
}
