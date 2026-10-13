/**
 * @file MLuaMap.h
 * @brief MLuaMap —— Lua 与 TMap<MScalarValue, MScalarValue> 之间的桥接器声明。
 *
 * @detail 同时给出 MScalarValue 作为有序/哈希容器 key 所需的 operator< 与
 *   std::hash 特化;metatable "MMapProxy" 提供 __len / __tostring / __pairs /
 *   __gc 与 __index 方法表。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Script/Lua/MLuaVector.h"

#include <cstring>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    /**
     * @brief operator< - 使 MScalarValue 可作为有序容器(std::less)的 key。
     * @param A 左操作数
     * @param B 右操作数
     * @return 先按 Type 的数值序,再按 Type 所指成员比较
     */
    inline bool operator<(const MScalarValue& A, const MScalarValue& B) {
        if (A.Type != B.Type) {
            return static_cast<uint8>(A.Type) < static_cast<uint8>(B.Type);
        }
        switch (A.Type) {
        case MScalarType::Int:
            return A.IntVal < B.IntVal;
        case MScalarType::Double:
            return A.DoubleVal < B.DoubleVal;
        case MScalarType::String:
            return A.StringVal < B.StringVal;
        }
        return false;
    }

} // namespace mession::script::lua

// std::hash 特化,放到 std 命名空间
namespace std {

    /**
     * @brief std::hash<MScalarValue> 特化 - 按 Type 分派到对应成员的哈希。
     *
     * @detail Double 用位模式(bit pattern)哈希,避免 +0.0 / -0.0 与 NaN 的不一致。
     */
    template <> struct hash<mession::script::lua::MScalarValue> {
        size_t operator()(const mession::script::lua::MScalarValue& V) const noexcept {
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

    /**
     * @brief MLuaMap - Lua 与 TMap<MScalarValue, MScalarValue> 的桥接器。
     *
     * @detail metatable "MMapProxy" 提供 __len / __tostring / __pairs / __gc 与
     *   __index 方法表(get/set/has/remove/size/clear/keys/values);
     *   另注册 "MMapIter" 作为迭代器 userdata 的 metatable。并未注册 __newindex。
     */
    class MLuaMap {
        public:
        /**
         * @brief Install - 注册 MMapProxy / MMapIter metatable 与 Mession.Map.new()。
         * @param L 目标 Lua 状态机
         */
        static void Install(lua_State* L);
    };

} // namespace mession::script::lua