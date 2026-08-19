#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Script/Lua/MLuaVector.h"

#include <cstring>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace mession::script::lua {

// MScalarValue 作为 unordered_map / map key 时需要的辅助
// std::less<MScalarValue> (有序 map)
inline bool operator<(const MScalarValue& A, const MScalarValue& B)
{
    if (A.Type != B.Type) {
        return static_cast<uint8>(A.Type) < static_cast<uint8>(B.Type);
    }
    switch (A.Type) {
    case MScalarType::Int:    return A.IntVal < B.IntVal;
    case MScalarType::Double: return A.DoubleVal < B.DoubleVal;
    case MScalarType::String: return A.StringVal < B.StringVal;
    }
    return false;
}

} // namespace mession::script::lua

// std::hash 特化,放到 std 命名空间
namespace std {

template <>
struct hash<mession::script::lua::MScalarValue> {
    size_t operator()(const mession::script::lua::MScalarValue& V) const noexcept
    {
        switch (V.Type) {
        case mession::script::lua::MScalarType::Int:
            return std::hash<int64>()(V.IntVal);
        case mession::script::lua::MScalarType::Double: {
            // double hash:用 bit pattern 防止 NaN 不一致
            uint64_t Bits = 0;
            std::memcpy(&Bits, &V.DoubleVal, sizeof(Bits));
            return std::hash<uint64>()(Bits);
        }
        case mession::script::lua::MScalarType::String:
            return std::hash<MString>()(V.StringVal);
        }
        return 0;
    }
};

} // namespace std

namespace mession::script::lua {

// MLuaMap — Lua ↔ TMap<MScalarValue, MScalarValue> 桥接器
// 用法:MLuaMap::Install(L) 注册 Mession.Map.new() + MMapProxy metatable
// 内部:userdata 持 TMap*,metatable 提供 __len / __pairs / __index / __newindex / __gc
//      + 方法 get/set/has/remove/size/clear/keys/values
class MLuaMap
{
public:
    static void Install(lua_State* L);
};

} // namespace mession::script::lua