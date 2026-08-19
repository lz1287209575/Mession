#include "Common/Script/Lua/MLuaMap.h"

#include <cstring>
#include <new>

namespace mession::script::lua {

namespace {

using FMap = TMap<MScalarValue, MScalarValue>;

// Iter userdata 内部结构
struct FMapIter {
    FMap::const_iterator Cur;
    FMap::const_iterator End;
};

inline const char* MMapProxyMetaName = "MMapProxy";
inline const char* MMapIterMetaName = "MMapIter";

FMap* GetMap(lua_State* L, int32 ArgNum = 1)
{
    return static_cast<FMap*>(luaL_checkudata(L, ArgNum, MMapProxyMetaName));
}

int MetaLen(lua_State* L)
{
    FMap* M = GetMap(L, 1);
    lua_pushinteger(L, static_cast<int32>(M->size()));
    return 1;
}

int MetaToString(lua_State* L)
{
    FMap* M = GetMap(L, 1);
    MString S = MString("Map(size=") + std::to_string(M->size()) + MString(")");
    lua_pushlstring(L, S.c_str(), S.size());
    return 1;
}

int MetaMapGC(lua_State* L)
{
    FMap* M = GetMap(L, 1);
    M->~FMap();
    return 0;
}

int MetaIterGC(lua_State* L)
{
    FMapIter* It = static_cast<FMapIter*>(luaL_checkudata(L, 1, MMapIterMetaName));
    It->~FMapIter();
    return 0;
}

int MetaPairs(lua_State* L)
{
    FMap* M = GetMap(L, 1);
    // 创建 iter userdata
    void* Ud = lua_newuserdata(L, sizeof(FMapIter));
    new (Ud) FMapIter{M->begin(), M->end()};
    luaL_setmetatable(L, MMapIterMetaName);
    // iter upvalue 顺序:(iter, map) — next 函数闭包,需要访问 map 与 iter
    // 由于 __pairs 必须返回 (iter, map, next_fn) 三元组;我们这里 next_fn 用 C closure
    // 替代方案:写一个 next_fn,迭代 iter 闭包。这里采用更简单做法 — __pairs 在 Lua 侧不可用,
    // 我们改用 Lua 函数通过 method: 暴露 next(iter)。但 Lua 5.4 supports generic for k,v in pairs(map)。
    // 实际策略:实现一个 SCRIPT 端的 pairs(map) 工厂函数(走 __pairs 不会自动触发 for 循环)。
    // 简化:把 iterator state 放在 metatable 的 upvalue 中。
    //
    // 设计:C closure 的 upvalue = map 指针(iter 状态随 next_fn 自维护 — 但跨调用不持久)
    // 为保证 persistence:把 iter 状态放在 userdata,作为第二个 upvalue。
    lua_pushvalue(L, 1); // push map
    // next_fn upvalue:1=iter userdata, 2=map
    lua_pushcfunction(L, [](lua_State* L) -> int {
        FMapIter* It2 = static_cast<FMapIter*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (It2->Cur == It2->End) return 0;
        MScalarValue K = It2->Cur->first;
        MScalarValue V = It2->Cur->second;
        ++(It2->Cur);
        K.PushToLua(L);
        V.PushToLua(L);
        return 2;
    });
    // stack: iter_ud, map, next_fn
    // Lua pairs expects: (iter_ud, state, next_fn)
    return 3;
}

/// @lua-stdlib Map.get
/// @lua-self MMap
/// @lua-param k MScalar
/// @lua-return MScalar|nil
int MethodGet(lua_State* L)
{
    FMap* M = GetMap(L, 1);
    auto Key = MScalarValue::FromLua(L, 2);
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

/// @lua-stdlib Map.set
/// @lua-self MMap
/// @lua-param k MScalar
/// @lua-param v MScalar
/// @lua-return nil
int MethodSet(lua_State* L)
{
    FMap* M = GetMap(L, 1);
    auto Key = MScalarValue::FromLua(L, 2);
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

/// @lua-stdlib Map.has
/// @lua-self MMap
/// @lua-param k MScalar
/// @lua-return boolean
int MethodHas(lua_State* L)
{
    FMap* M = GetMap(L, 1);
    auto Key = MScalarValue::FromLua(L, 2);
    if (Key.IsErr()) {
        return luaL_error(L, "unsupported_key_type");
    }
    lua_pushboolean(L, M->count(Key.GetValue()) != 0 ? 1 : 0);
    return 1;
}

/// @lua-stdlib Map.remove
/// @lua-self MMap
/// @lua-param k MScalar
/// @lua-return MScalar|nil
int MethodRemove(lua_State* L)
{
    FMap* M = GetMap(L, 1);
    auto Key = MScalarValue::FromLua(L, 2);
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

/// @lua-stdlib Map.size
/// @lua-self MMap
/// @lua-return integer
int MethodSize(lua_State* L)
{
    FMap* M = GetMap(L, 1);
    lua_pushinteger(L, static_cast<int32>(M->size()));
    return 1;
}

/// @lua-stdlib Map.clear
/// @lua-self MMap
/// @lua-return nil
int MethodClear(lua_State* L)
{
    FMap* M = GetMap(L, 1);
    M->clear();
    return 0;
}

/// @lua-stdlib Map.keys
/// @lua-self MMap
/// @lua-return {MScalar}
int MethodKeys(lua_State* L)
{
    FMap* M = GetMap(L, 1);
    lua_newtable(L);
    int Idx = 1;
    for (const auto& Pair : *M) {
        Pair.first.PushToLua(L);
        lua_rawseti(L, -2, Idx++);
    }
    return 1;
}

/// @lua-stdlib Map.values
/// @lua-self MMap
/// @lua-return {MScalar}
int MethodValues(lua_State* L)
{
    FMap* M = GetMap(L, 1);
    lua_newtable(L);
    int Idx = 1;
    for (const auto& Pair : *M) {
        Pair.second.PushToLua(L);
        lua_rawseti(L, -2, Idx++);
    }
    return 1;
}

/// @lua-stdlib Map.new
/// @lua-return MMap
int FactoryNew(lua_State* L)
{
    void* Ud = lua_newuserdata(L, sizeof(FMap));
    new (Ud) FMap();
    luaL_setmetatable(L, MMapProxyMetaName);
    return 1;
}

void RegisterMetaMethods(lua_State* L)
{
    // Map metatable
    luaL_newmetatable(L, MMapProxyMetaName);
    lua_pushcfunction(L, MetaLen);       lua_setfield(L, -2, "__len");
    lua_pushcfunction(L, MetaToString);  lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, MetaPairs);     lua_setfield(L, -2, "__pairs");
    lua_pushcfunction(L, MetaMapGC);     lua_setfield(L, -2, "__gc");
    lua_newtable(L);
    lua_pushcfunction(L, MethodGet);     lua_setfield(L, -2, "get");
    lua_pushcfunction(L, MethodSet);     lua_setfield(L, -2, "set");
    lua_pushcfunction(L, MethodHas);     lua_setfield(L, -2, "has");
    lua_pushcfunction(L, MethodRemove);  lua_setfield(L, -2, "remove");
    lua_pushcfunction(L, MethodSize);    lua_setfield(L, -2, "size");
    lua_pushcfunction(L, MethodClear);   lua_setfield(L, -2, "clear");
    lua_pushcfunction(L, MethodKeys);    lua_setfield(L, -2, "keys");
    lua_pushcfunction(L, MethodValues);  lua_setfield(L, -2, "values");
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    // Iter metatable
    luaL_newmetatable(L, MMapIterMetaName);
    lua_pushcfunction(L, MetaIterGC);    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);
}

} // namespace

void MLuaMap::Install(lua_State* L)
{
    RegisterMetaMethods(L);

    lua_getglobal(L, "Mession");
    if (lua_isnil(L, -1)) {
        lua_newtable(L);
        lua_setglobal(L, "Mession");
        lua_getglobal(L, "Mession");
    }
    lua_newtable(L);
    lua_pushcfunction(L, FactoryNew);
    lua_setfield(L, -2, "new");
    lua_setfield(L, -2, "Map");
    lua_pop(L, 1);
}

} // namespace mession::script::lua