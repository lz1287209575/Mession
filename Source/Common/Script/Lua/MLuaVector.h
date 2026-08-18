#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace mession::script::lua {

enum class MScalarType : uint8
{
    Int    = 0,
    Double = 1,
    String = 2,
};

// MScalarValue — C++ 与 Lua 之间的标量值统一容器
// 引用 Vector / Map / 反射参数 等多个 spec 都用
struct MScalarValue
{
    MScalarType Type = MScalarType::Int;
    int64       IntVal    = 0;
    double      DoubleVal = 0.0;
    MString     StringVal;

    static MScalarValue MakeInt(int64 V)
    {
        MScalarValue S;
        S.Type    = MScalarType::Int;
        S.IntVal  = V;
        return S;
    }

    static MScalarValue MakeDouble(double V)
    {
        MScalarValue S;
        S.Type      = MScalarType::Double;
        S.DoubleVal = V;
        return S;
    }

    static MScalarValue MakeString(MString V)
    {
        MScalarValue S;
        S.Type      = MScalarType::String;
        S.StringVal = std::move(V);
        return S;
    }

    // 从 Lua 栈 idx 位置弹出标量;不支持的类型返回 Err
    static TResult<MScalarValue> FromLua(lua_State* L, int32 Index);

    // 推 Lua 栈
    void PushToLua(lua_State* L) const;

    // 调试用字符串(测试预期比较)
    MString ToDebugString() const;
};

inline bool operator==(const MScalarValue& A, const MScalarValue& B)
{
    if (A.Type != B.Type) return false;
    switch (A.Type)
    {
    case MScalarType::Int:    return A.IntVal == B.IntVal;
    case MScalarType::Double: return A.DoubleVal == B.DoubleVal;
    case MScalarType::String: return A.StringVal == B.StringVal;
    }
    return false;
}

inline bool operator!=(const MScalarValue& A, const MScalarValue& B)
{
    return !(A == B);
}

} // namespace mession::script::lua