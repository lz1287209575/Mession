/**
 * @file MLuaMap.cpp
 * @brief 实现 MLuaMap:MMapProxy / MMapIter metatable 与 Mession.Map.* 入口。
 */
#include "Common/Script/Lua/MLuaMap.h"

#include <cstring>
#include <new>

namespace mession::script::lua {

    namespace {

        /** @brief FMap - MMap 代理内部持有的容器类型 */
        using FMap = TMap<MScalarValue, MScalarValue>;

        /**
         * @brief FMapIter - __pairs 迭代器 userdata:当前迭代位置 + 结束迭代器。
         *
         * @detail 持有 TMap 的 const_iterator;迭代期间被迭代的 map 必须存活。
         */
        struct FMapIter {
            FMap::const_iterator Cur;
            FMap::const_iterator End;
        };

        inline const char* MMapProxyMetaName = "MMapProxy";
        inline const char* MMapIterMetaName  = "MMapIter";

        /**
         * @brief GetMap - 从 1 号(或 ArgNum)参数取 MMapProxy userdata。
         * @param L      目标 Lua 状态机
         * @param ArgNum 参数槽位,默认 1
         * @return 指向 userdata 内 FMap 的指针;类型不符时 luaL_checkudata 抛错
         */
        FMap* GetMap(lua_State* L, int32 ArgNum = 1) {
            return static_cast<FMap*>(luaL_checkudata(L, ArgNum, MMapProxyMetaName));
        }

        /**
         * @brief MetaLen - __len;返回键值对个数。
         *
         * @detail NOTE: size() 被截断为 int32 后压栈(项数超过 2^31 时结果不可靠)。
         */
        int MetaLen(lua_State* L) {
            FMap* M = GetMap(L, 1);
            lua_pushinteger(L, static_cast<int32>(M->size()));
            return 1;
        }

        /** @brief MetaToString - __tostring;输出 "Map(size=N)" */
        int MetaToString(lua_State* L) {
            FMap*   M = GetMap(L, 1);
            MString S = MString("Map(size=") + std::to_string(M->size()) + MString(")");
            lua_pushlstring(L, S.c_str(), S.size());
            return 1;
        }

        /** @brief MetaMapGC - __gc;显式调用 ~FMap() */
        int MetaMapGC(lua_State* L) {
            FMap* M = GetMap(L, 1);
            M->~FMap();
            return 0;
        }

        /** @brief MetaIterGC - MMapIter 的 __gc;显式调用 ~FMapIter() */
        int MetaIterGC(lua_State* L) {
            FMapIter* It = static_cast<FMapIter*>(luaL_checkudata(L, 1, MMapIterMetaName));
            It->~FMapIter();
            return 0;
        }

        /**
         * @brief MetaPairs - __pairs;返回迭代三元组 (next_fn, iter_ud, nil)。
         *
         * @detail Lua 5.4 契约: __pairs 返回 (next_fn, state, control),for 每步调
         *   next_fn(state, control);此处 state 为 iter userdata。旧实现返回顺序
         *   (iter_ud, map, next_fn) 是错的 —— for 会把 iter_ud 当函数调,报
         *   attempt to call a MMapIter value。
         */
        int MetaPairs(lua_State* L) {
            FMap* M = GetMap(L, 1);
            // 创建 iter userdata
            void* Ud = lua_newuserdata(L, sizeof(FMapIter));
            new (Ud) FMapIter{M->begin(), M->end()};
            luaL_setmetatable(L, MMapIterMetaName);
            // push map 仅用于构造返回栈(下方重排时会删掉);next_fn 不使用 upvalue:
            // pushcfunction 等价于 pushcclosure(0 upvalue),早期注释误以为 upvalue 1=iter
            // 却从未注册,导致 upvalueindex(1) 读到无效指针 → SIGSEGV。
            lua_pushvalue(L, 1); // push map(仅作为 pairs 返回栈的一部分,见下方重排)
            lua_pushcfunction(L, [](lua_State* L) -> int {
                FMapIter* It2 = static_cast<FMapIter*>(lua_touserdata(L, 1));
                if (!It2 || It2->Cur == It2->End)
                    return 0;
                MScalarValue K = It2->Cur->first;
                MScalarValue V = It2->Cur->second;
                ++(It2->Cur);
                K.PushToLua(L);
                V.PushToLua(L);
                return 2;
            });
            // 进入时栈=[m];经 newuserdata + pushvalue(map) + pushcfunction(next)
            // → 栈=[m, iter_ud, map, next_fn](共 4 个,m 仍在)。
            // 重排为 pairs 契约要求的 (next_fn, state=iter_ud, control=nil)。
            lua_remove(L, 1); // 删 m   → [iter_ud, map, next_fn]
            lua_remove(L, 2); // 删 map → [iter_ud, next_fn]
            lua_insert(L, 1); // next 提前 → [next_fn, iter_ud]
            lua_pushnil(L);   // control → [next_fn, iter_ud, nil]
            return 3;
        }

        /** @brief MethodGet - 取键对应的值;键不存在返回 nil */
        /// @lua-stdlib Map.get
        /// @lua-self MMap
        /// @lua-param k MScalar
        /// @lua-return MScalar|nil
        int MethodGet(lua_State* L) {
            FMap* M   = GetMap(L, 1);
            auto  Key = MScalarValue::FromLua(L, 2);
            if (Key.IsErr()) {
                return luaL_error(L, "unsupported_key_type");
            }
            auto It = M->find(Key.GetValue());
            if (It == M->end()) {
                lua_pushnil(L);
                return 1;
            }
            It->second.PushToLua(L);
            return 1;
        }

        /** @brief MethodSet - 写入一个键值对 */
        /// @lua-stdlib Map.set
        /// @lua-self MMap
        /// @lua-param k MScalar
        /// @lua-param v MScalar
        /// @lua-return nil
        int MethodSet(lua_State* L) {
            FMap* M   = GetMap(L, 1);
            auto  Key = MScalarValue::FromLua(L, 2);
            if (Key.IsErr()) {
                return luaL_error(L, "unsupported_key_type");
            }
            auto Val = MScalarValue::FromLua(L, 3);
            if (Val.IsErr()) {
                return luaL_error(L, "unsupported_value_type");
            }
            (*M)[Key.GetValue()] = Val.GetValue();
            return 0;
        }

        /** @brief MethodHas - 判定键是否存在 */
        /// @lua-stdlib Map.has
        /// @lua-self MMap
        /// @lua-param k MScalar
        /// @lua-return boolean
        int MethodHas(lua_State* L) {
            FMap* M   = GetMap(L, 1);
            auto  Key = MScalarValue::FromLua(L, 2);
            if (Key.IsErr()) {
                return luaL_error(L, "unsupported_key_type");
            }
            lua_pushboolean(L, M->count(Key.GetValue()) != 0 ? 1 : 0);
            return 1;
        }

        /** @brief MethodRemove - 移除键并返回其值;键不存在返回 nil */
        /// @lua-stdlib Map.remove
        /// @lua-self MMap
        /// @lua-param k MScalar
        /// @lua-return MScalar|nil
        int MethodRemove(lua_State* L) {
            FMap* M   = GetMap(L, 1);
            auto  Key = MScalarValue::FromLua(L, 2);
            if (Key.IsErr()) {
                return luaL_error(L, "unsupported_key_type");
            }
            auto It = M->find(Key.GetValue());
            if (It == M->end()) {
                lua_pushnil(L);
                return 1;
            }
            It->second.PushToLua(L);
            M->erase(It);
            return 1;
        }

        /** @brief MethodSize - 返回键值对个数 */
        /// @lua-stdlib Map.size
        /// @lua-self MMap
        /// @lua-return integer
        int MethodSize(lua_State* L) {
            FMap* M = GetMap(L, 1);
            lua_pushinteger(L, static_cast<int32>(M->size()));
            return 1;
        }

        /** @brief MethodClear - 清空所有键值对 */
        /// @lua-stdlib Map.clear
        /// @lua-self MMap
        /// @lua-return nil
        int MethodClear(lua_State* L) {
            FMap* M = GetMap(L, 1);
            M->clear();
            return 0;
        }

        /** @brief MethodKeys - 以 Lua 数组表返回全部键 */
        /// @lua-stdlib Map.keys
        /// @lua-self MMap
        /// @lua-return {MScalar}
        int MethodKeys(lua_State* L) {
            FMap* M = GetMap(L, 1);
            lua_newtable(L);
            int Idx = 1;
            for (const auto& Pair : *M) {
                Pair.first.PushToLua(L);
                lua_rawseti(L, -2, Idx++);
            }
            return 1;
        }

        /** @brief MethodValues - 以 Lua 数组表返回全部值 */
        /// @lua-stdlib Map.values
        /// @lua-self MMap
        /// @lua-return {MScalar}
        int MethodValues(lua_State* L) {
            FMap* M = GetMap(L, 1);
            lua_newtable(L);
            int Idx = 1;
            for (const auto& Pair : *M) {
                Pair.second.PushToLua(L);
                lua_rawseti(L, -2, Idx++);
            }
            return 1;
        }

        /** @brief FactoryNew - 构造空的 MMap */
        /// @lua-stdlib Map.new
        /// @lua-return MMap
        int FactoryNew(lua_State* L) {
            void* Ud = lua_newuserdata(L, sizeof(FMap));
            new (Ud) FMap();
            luaL_setmetatable(L, MMapProxyMetaName);
            return 1;
        }

        /**
         * @brief RegisterMetaMethods - 注册 MMapProxy / MMapIter 两个 metatable。
         * @param L 目标 Lua 状态机
         *
         * @detail MMapProxy:__len / __tostring / __pairs / __gc + __index 方法表;
         *   MMapIter:仅 __gc。
         */
        void RegisterMetaMethods(lua_State* L) {
            // Map 的 metatable
            luaL_newmetatable(L, MMapProxyMetaName);
            lua_pushcfunction(L, MetaLen);
            lua_setfield(L, -2, "__len");
            lua_pushcfunction(L, MetaToString);
            lua_setfield(L, -2, "__tostring");
            lua_pushcfunction(L, MetaPairs);
            lua_setfield(L, -2, "__pairs");
            lua_pushcfunction(L, MetaMapGC);
            lua_setfield(L, -2, "__gc");
            lua_newtable(L);
            lua_pushcfunction(L, MethodGet);
            lua_setfield(L, -2, "get");
            lua_pushcfunction(L, MethodSet);
            lua_setfield(L, -2, "set");
            lua_pushcfunction(L, MethodHas);
            lua_setfield(L, -2, "has");
            lua_pushcfunction(L, MethodRemove);
            lua_setfield(L, -2, "remove");
            lua_pushcfunction(L, MethodSize);
            lua_setfield(L, -2, "size");
            lua_pushcfunction(L, MethodClear);
            lua_setfield(L, -2, "clear");
            lua_pushcfunction(L, MethodKeys);
            lua_setfield(L, -2, "keys");
            lua_pushcfunction(L, MethodValues);
            lua_setfield(L, -2, "values");
            lua_setfield(L, -2, "__index");
            lua_pop(L, 1);

            // Iter 的 metatable
            luaL_newmetatable(L, MMapIterMetaName);
            lua_pushcfunction(L, MetaIterGC);
            lua_setfield(L, -2, "__gc");
            lua_pop(L, 1);
        }

    } // namespace

    /**
     * @brief Install - 注册 metatable 与 Mession.Map.new()。
     * @param L 目标 Lua 状态机
     */
    void MLuaMap::Install(lua_State* L) {
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
        lua_setfield(L, -2, "Map");
        lua_pop(L, 1);
    }

} // namespace mession::script::lua