/**
 * @file MLuaVector.cpp
 * @brief 实现 MLuaVector / MScalarValue:标量封装与 MVectorProxy metatable 注册。
 */
#include "Common/Script/Lua/MLuaVector.h"
#include "Common/Runtime/Log/Log.h"

#include <new>

namespace mession::script::lua {

    TResult<MScalarValue> MScalarValue::FromLua(lua_State* L, int32 Index) {
        int Type = lua_type(L, Index);
        switch (Type) {
        case LUA_TNIL:
            return TResult<MScalarValue>::Ok(MScalarValue::MakeInt(0));
        case LUA_TBOOLEAN:
            return TResult<MScalarValue>::Ok(MScalarValue::MakeInt(lua_toboolean(L, Index) ? 1 : 0));
        case LUA_TNUMBER:
            return TResult<MScalarValue>::Ok(MScalarValue::MakeDouble(lua_tonumber(L, Index)));
        case LUA_TSTRING: {
            size_t      Len = 0;
            const char* P   = lua_tolstring(L, Index, &Len);
            return TResult<MScalarValue>::Ok(MScalarValue::MakeString(MString(P, Len)));
        }
        default:
            return TResult<MScalarValue>::Err(MString("unsupported_type"));
        }
    }

    void MScalarValue::PushToLua(lua_State* L) const {
        switch (Type) {
        case MScalarType::Int:
            lua_pushinteger(L, IntVal);
            break;
        case MScalarType::Double:
            lua_pushnumber(L, DoubleVal);
            break;
        case MScalarType::String:
            lua_pushlstring(L, StringVal.c_str(), StringVal.size());
            break;
        }
    }

    MString MScalarValue::ToDebugString() const {
        switch (Type) {
        case MScalarType::Int:
            return MString("Int(") + std::to_string(IntVal) + MString(")");
        case MScalarType::Double:
            return MString("Double(") + std::to_string(DoubleVal) + MString(")");
        case MScalarType::String:
            return MString("String(") + StringVal + MString(")");
        }
        return MString("Unknown");
    }

    namespace {

        /**
         * @brief GetVector - 从 1 号(或 ArgNum)参数取 MVectorProxy userdata。
         * @param L      目标 Lua 状态机
         * @param ArgNum 参数槽位,默认 1
         * @return 指向 userdata 内 TVector<MScalarValue> 的指针;类型不符时 luaL_checkudata 抛错
         */
        TVector<MScalarValue>* GetVector(lua_State* L, int32 ArgNum = 1) {
            return static_cast<TVector<MScalarValue>*>(luaL_checkudata(L, ArgNum, "MVectorProxy"));
        }

        inline const char* MVectorProxyMetaName = "MVectorProxy";

        /**
         * @brief MetaLen - __len;返回元素个数。
         *
         * @detail NOTE: size() 被截断为 int32 后压栈(元素数超过 2^31 时结果不可靠)。
         */
        int MetaLen(lua_State* L) {
            TVector<MScalarValue>* V = GetVector(L, 1);
            lua_pushinteger(L, static_cast<int32>(V->size()));
            return 1;
        }

        /** @brief MetaToString - __tostring;输出 "Vector(size=N)" */
        int MetaToString(lua_State* L) {
            TVector<MScalarValue>* V = GetVector(L, 1);
            MString                S = MString("Vector(size=") + std::to_string(V->size()) + MString(")");
            lua_pushlstring(L, S.c_str(), S.size());
            return 1;
        }

        /** @brief MetaEq - __eq;长度相同且逐元素 MScalarValue::operator== 相等才为真 */
        int MetaEq(lua_State* L) {
            TVector<MScalarValue>* A = GetVector(L, 1);
            TVector<MScalarValue>* B = GetVector(L, 2);
            if (A->size() != B->size()) {
                lua_pushboolean(L, 0);
                return 1;
            }
            for (size_t i = 0; i < A->size(); ++i) {
                if (!((*A)[i] == (*B)[i])) {
                    lua_pushboolean(L, 0);
                    return 1;
                }
            }
            lua_pushboolean(L, 1);
            return 1;
        }

        namespace {
            /**
             * @brief FVecIterState - __pairs 迭代器 state:vec 指针 + 下一个 1-based 下标。
             *
             * @detail 存于独立 userdata;for 循环期间循环变量持有 vec,裸指针安全。
             */
            struct FVecIterState {
                TVector<MScalarValue>* Vec;
                int                    Next; // 1-based
            };
        } // namespace

        /**
         * @brief MetaPairs - __pairs;返回迭代三元组 (next_fn, state, nil)。
         *
         * @detail Lua 5.4 的 for-in 契约: __pairs 返回 (next_fn, state, control),
         *   每步调 next_fn(state, control)。旧实现 pushcfunction 注册 0 个 upvalue
         *   却读 upvalueindex(1/2)(无效指针),且返回顺序错误。
         */
        int MetaPairs(lua_State* L) {
            TVector<MScalarValue>* V = GetVector(L, 1);

            // state userdata: 持 vec 指针 + 游标。vec 的 TVector 本体在 vec userdata
            // 里,for 循环期间 m 一定存活(循环变量被 Lua 持有),裸指针安全。
            auto* St = static_cast<FVecIterState*>(lua_newuserdata(L, sizeof(FVecIterState)));
            St->Vec  = V;
            St->Next = 1;
            luaL_newmetatable(L, "MVectorIter");
            lua_setmetatable(L, -2); // 栈: [vec, state_ud]

            lua_pushcfunction(L, [](lua_State* L) -> int {
                auto* St2 = static_cast<FVecIterState*>(lua_touserdata(L, 1)); // state
                if (!St2 || St2->Next > static_cast<int>(St2->Vec->size())) {
                    return 0; // 迭代结束
                }
                lua_pushinteger(L, St2->Next);           // key(1-based)
                (*St2->Vec)[St2->Next - 1].PushToLua(L); // value
                St2->Next++;
                return 2;
            });
            // 栈: [vec, state_ud, next_fn] → 调整为 (next_fn, state_ud, nil)
            lua_remove(L, 1); // 删 vec   → [state_ud, next_fn]
            lua_insert(L, 1); // next 提前 → [next_fn, state_ud]
            lua_pushnil(L);   // control  → [next_fn, state_ud, nil]
            return 3;
        }

        /** @brief MethodGet - 取 1-based 下标处元素;越界抛 out_of_range */
        /// @lua-stdlib Vector.get
        /// @lua-self MVector
        /// @lua-param i integer
        /// @lua-return MScalar
        int MethodGet(lua_State* L) {
            TVector<MScalarValue>* V   = GetVector(L, 1);
            int32                  Idx = static_cast<int32>(luaL_checkinteger(L, 2));
            if (Idx < 1 || Idx > static_cast<int32>(V->size())) {
                return luaL_error(L, "out_of_range");
            }
            (*V)[Idx - 1].PushToLua(L);
            return 1;
        }

        /** @brief MethodSet - 写 1-based 下标处元素;越界抛 out_of_range */
        /// @lua-stdlib Vector.set
        /// @lua-self MVector
        /// @lua-param i integer
        /// @lua-param v MScalar
        /// @lua-return nil
        int MethodSet(lua_State* L) {
            TVector<MScalarValue>* V   = GetVector(L, 1);
            int32                  Idx = static_cast<int32>(luaL_checkinteger(L, 2));
            if (Idx < 1 || Idx > static_cast<int32>(V->size())) {
                return luaL_error(L, "out_of_range");
            }
            auto Scalar = MScalarValue::FromLua(L, 3);
            if (Scalar.IsErr()) {
                return luaL_error(L, "unsupported_type");
            }
            (*V)[Idx - 1] = Scalar.GetValue();
            return 0;
        }

        /** @brief MethodPush - 尾部追加一个元素 */
        /// @lua-stdlib Vector.push
        /// @lua-self MVector
        /// @lua-param v MScalar
        /// @lua-return nil
        int MethodPush(lua_State* L) {
            TVector<MScalarValue>* V      = GetVector(L, 1);
            auto                   Scalar = MScalarValue::FromLua(L, 2);
            if (Scalar.IsErr()) {
                return luaL_error(L, "unsupported_type");
            }
            V->push_back(Scalar.GetValue());
            return 0;
        }

        /** @brief MethodPop - 弹出尾部元素;空表返回 nil */
        /// @lua-stdlib Vector.pop
        /// @lua-self MVector
        /// @lua-return MScalar|nil
        int MethodPop(lua_State* L) {
            TVector<MScalarValue>* V = GetVector(L, 1);
            if (V->empty()) {
                lua_pushnil(L);
                return 1;
            }
            MScalarValue Out = V->back();
            V->pop_back();
            Out.PushToLua(L);
            return 1;
        }

        /** @brief MethodSize - 返回元素个数 */
        /// @lua-stdlib Vector.size
        /// @lua-self MVector
        /// @lua-return integer
        int MethodSize(lua_State* L) {
            TVector<MScalarValue>* V = GetVector(L, 1);
            lua_pushinteger(L, static_cast<int32>(V->size()));
            return 1;
        }

        /** @brief MethodInsert - 在下标 i 处插入元素(允许 i == size+1) */
        /// @lua-stdlib Vector.insert
        /// @lua-self MVector
        /// @lua-param i integer
        /// @lua-param v MScalar
        /// @lua-return nil
        int MethodInsert(lua_State* L) {
            TVector<MScalarValue>* V   = GetVector(L, 1);
            int32                  Idx = static_cast<int32>(luaL_checkinteger(L, 2));
            if (Idx < 1 || Idx > static_cast<int32>(V->size()) + 1) {
                return luaL_error(L, "out_of_range");
            }
            auto Scalar = MScalarValue::FromLua(L, 3);
            if (Scalar.IsErr()) {
                return luaL_error(L, "unsupported_type");
            }
            V->insert(V->begin() + (Idx - 1), Scalar.GetValue());
            return 0;
        }

        /** @brief MethodRemove - 移除并返回下标 i 处元素 */
        /// @lua-stdlib Vector.remove
        /// @lua-self MVector
        /// @lua-param i integer
        /// @lua-return MScalar
        int MethodRemove(lua_State* L) {
            TVector<MScalarValue>* V   = GetVector(L, 1);
            int32                  Idx = static_cast<int32>(luaL_checkinteger(L, 2));
            if (Idx < 1 || Idx > static_cast<int32>(V->size())) {
                return luaL_error(L, "out_of_range");
            }
            MScalarValue Out = (*V)[Idx - 1];
            V->erase(V->begin() + (Idx - 1));
            Out.PushToLua(L);
            return 1;
        }

        /** @brief MethodClear - 清空所有元素 */
        /// @lua-stdlib Vector.clear
        /// @lua-self MVector
        /// @lua-return nil
        int MethodClear(lua_State* L) {
            TVector<MScalarValue>* V = GetVector(L, 1);
            V->clear();
            return 0;
        }

        /** @brief MethodClone - 深拷贝出一个新 MVector */
        /// @lua-stdlib Vector.clone
        /// @lua-self MVector
        /// @lua-return MVector
        int MethodClone(lua_State* L) {
            TVector<MScalarValue>* V  = GetVector(L, 1);
            void*                  Ud = lua_newuserdata(L, sizeof(TVector<MScalarValue>));
            new (Ud) TVector<MScalarValue>(*V);
            luaL_setmetatable(L, MVectorProxyMetaName);
            return 1;
        }

        /** @brief MetaGC - __gc;显式调用 ~TVector<MScalarValue>() */
        int MetaGC(lua_State* L) {
            TVector<MScalarValue>* V = GetVector(L, 1);
            V->~TVector<MScalarValue>();
            return 0;
        }

        /** @brief FactoryNew - 构造 MVector;可传 (size, init) 预填充 */
        /// @lua-stdlib Vector.new
        /// @lua-param size? integer
        /// @lua-param init? MScalar
        /// @lua-return MVector
        int FactoryNew(lua_State* L) {
            int32 Size = 0;
            if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
                Size = static_cast<int32>(luaL_checkinteger(L, 1));
                if (Size < 0) {
                    return luaL_error(L, "size_must_be_non_negative");
                }
            }

            void* Ud = lua_newuserdata(L, sizeof(TVector<MScalarValue>));
            auto  V  = new (Ud) TVector<MScalarValue>();
            V->reserve(Size);
            if (Size > 0 && lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
                auto Init = MScalarValue::FromLua(L, 2);
                if (Init.IsErr()) {
                    return luaL_error(L, "unsupported_type");
                }
                for (int32 i = 0; i < Size; ++i) {
                    V->push_back(Init.GetValue());
                }
            }

            luaL_setmetatable(L, MVectorProxyMetaName);
            return 1;
        }

        /**
         * @brief RegisterMetaMethods - 注册 MVectorProxy metatable 与 __index 方法表。
         * @param L 目标 Lua 状态机
         *
         * @detail 注册 __len / __tostring / __eq / __pairs / __gc,以及
         *   get/set/push/pop/size/insert/remove/clear/clone 九个方法。
         */
        void RegisterMetaMethods(lua_State* L) {
            luaL_newmetatable(L, MVectorProxyMetaName);

            lua_pushcfunction(L, MetaLen);
            lua_setfield(L, -2, "__len");
            lua_pushcfunction(L, MetaToString);
            lua_setfield(L, -2, "__tostring");
            lua_pushcfunction(L, MetaEq);
            lua_setfield(L, -2, "__eq");
            lua_pushcfunction(L, MetaPairs);
            lua_setfield(L, -2, "__pairs");
            lua_pushcfunction(L, MetaGC);
            lua_setfield(L, -2, "__gc");

            lua_newtable(L);
            lua_pushcfunction(L, MethodGet);
            lua_setfield(L, -2, "get");
            lua_pushcfunction(L, MethodSet);
            lua_setfield(L, -2, "set");
            lua_pushcfunction(L, MethodPush);
            lua_setfield(L, -2, "push");
            lua_pushcfunction(L, MethodPop);
            lua_setfield(L, -2, "pop");
            lua_pushcfunction(L, MethodSize);
            lua_setfield(L, -2, "size");
            lua_pushcfunction(L, MethodInsert);
            lua_setfield(L, -2, "insert");
            lua_pushcfunction(L, MethodRemove);
            lua_setfield(L, -2, "remove");
            lua_pushcfunction(L, MethodClear);
            lua_setfield(L, -2, "clear");
            lua_pushcfunction(L, MethodClone);
            lua_setfield(L, -2, "clone");
            lua_setfield(L, -2, "__index");
            lua_pop(L, 1);
        }

    } // namespace

    /**
     * @brief Install - 注册 metatable 与 Mession.Vector.new()。
     * @param L 目标 Lua 状态机
     */
    void MLuaVector::Install(lua_State* L) {
        RegisterMetaMethods(L);

        lua_getglobal(L, "Mession");
        if (lua_isnil(L, -1)) {
            // 弹掉 getglobal 压入的 nil,再重建 Mession 表并复制引用:
            // setglobal 弹副本,栈上留下真表 —— 与表已存在分支栈净变化一致(+1),
            // 修复原实现残留 nil 导致的调用方栈索引错位。
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setglobal(L, "Mession");
        }
        lua_newtable(L);
        lua_pushcfunction(L, FactoryNew);
        lua_setfield(L, -2, "new");
        lua_setfield(L, -2, "Vector");
        lua_pop(L, 1);
    }

} // namespace mession::script::lua
