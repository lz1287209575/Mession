#pragma once

#include "Common/Runtime/MLib.h"

// Result/Error 类型 - 统一错误返回
template<typename T, typename E = MString>
struct TResult
{
    TOptional<T> Value;
    TOptional<E> Error;

    static TResult Ok(T InValue)
    {
        TResult R;
        R.Value = InValue;
        return R;
    }

    static TResult Err(E InError)
    {
        TResult R;
        R.Error = InError;
        return R;
    }

    bool IsOk() const { return Value.has_value(); }
    bool IsErr() const { return Error.has_value(); }
    T& GetValue() { return *Value; }
    const T& GetValue() const { return *Value; }
    E& GetError() { return *Error; }
    const E& GetError() const { return *Error; }
};

// void 特化：仅表示成功/失败
template<typename E>
struct TResult<void, E>
{
    bool bSuccess = false;
    TOptional<E> Error;

    static TResult Ok()
    {
        TResult R;
        R.bSuccess = true;
        return R;
    }

    static TResult Err(E InError)
    {
        TResult R;
        R.bSuccess = false;
        R.Error = InError;
        return R;
    }

    bool IsOk() const { return bSuccess; }
    bool IsErr() const { return Error.has_value(); }
    E& GetError() { return *Error; }
    const E& GetError() const { return *Error; }
};