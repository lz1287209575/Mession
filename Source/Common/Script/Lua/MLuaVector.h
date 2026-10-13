/**
 * @file MLuaVector.h
 * @brief MLuaVector —— Lua 与 TVector<MScalarValue> 之间的桥接器声明。
 *
 * @detail userdata 持有 TVector<MScalarValue>*;metatable "MVectorProxy" 提供
 *   __len / __tostring / __eq / __pairs / __gc 与 __index 方法表。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    /**
     * @brief MScalarType - 标量的运行时类型标签,决定 MScalarValue 哪个成员有效。
     */
    enum class MScalarType : uint8 {
        Int    = 0, // 有效成员:IntVal
        Double = 1, // 有效成员:DoubleVal
        String = 2, // 有效成员:StringVal
    };

    /**
     * @brief MScalarValue - C++ 与 Lua 之间的标量值统一容器(tagged union 语义)。
     *
     * @detail 只有 Type 指明的那个成员有意义;Vector / Map / 反射参数等多处复用。
     */
    struct MScalarValue {
        MScalarType Type      = MScalarType::Int;
        int64       IntVal    = 0;
        double      DoubleVal = 0.0;
        MString     StringVal;

        static MScalarValue MakeInt(int64 V) {
            MScalarValue S;
            S.Type   = MScalarType::Int;
            S.IntVal = V;
            return S;
        }

        static MScalarValue MakeDouble(double V) {
            MScalarValue S;
            S.Type      = MScalarType::Double;
            S.DoubleVal = V;
            return S;
        }

        static MScalarValue MakeString(MString V) {
            MScalarValue S;
            S.Type      = MScalarType::String;
            S.StringVal = std::move(V);
            return S;
        }

        /**
         * @brief FromLua - 读取 Lua 栈 Index 槽位并转成标量(只读,不弹栈)。
         * @param L     目标 Lua 状态机
         * @param Index 栈索引(1-based 或负值相对)
         * @return nil/boolean 转 Int,number 转 Double,string 转 String;其余类型 Err("unsupported_type")
         */
        static TResult<MScalarValue> FromLua(lua_State* L, int32 Index);

        /**
         * @brief PushToLua - 按 Type 把当前值压入 Lua 栈。
         * @param L 目标 Lua 状态机
         */
        void PushToLua(lua_State* L) const;

        /**
         * @brief ToDebugString - 生成调试串。
         * @return "Int(1)" / "Double(1.5)" / "String(s)";Type 未知时返回 "Unknown"
         */
        MString ToDebugString() const;
    };

    /** @brief operator== - 先比 Type,再比 Type 所指成员的值 */
    inline bool operator==(const MScalarValue& A, const MScalarValue& B) {
        if (A.Type != B.Type)
            return false;
        switch (A.Type) {
        case MScalarType::Int:
            return A.IntVal == B.IntVal;
        case MScalarType::Double:
            return A.DoubleVal == B.DoubleVal;
        case MScalarType::String:
            return A.StringVal == B.StringVal;
        }
        return false;
    }

    /** @brief operator!= - operator== 取反 */
    inline bool operator!=(const MScalarValue& A, const MScalarValue& B) {
        return !(A == B);
    }

    /**
     * @brief MLuaVector - Lua 与 TVector<MScalarValue> 的桥接器。
     *
     * @detail metatable "MVectorProxy" 实际注册 __len / __tostring / __eq / __pairs /
     *   __gc 与 __index 方法表;并未注册 __newindex。
     */
    class MLuaVector {
        public:
        /**
         * @brief Install - 注册 MVectorProxy metatable 与 Mession.Vector.new()。
         * @param L 目标 Lua 状态机
         *
         * @detail 应在 MLuaEngine::Initialize 中、执行业务 lua 代码之前调用。
         */
        static void Install(lua_State* L);
    };

} // namespace mession::script::lua