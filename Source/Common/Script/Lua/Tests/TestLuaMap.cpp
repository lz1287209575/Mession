#include "Common/Script/Lua/MLuaMap.h"
#include "Common/Script/Lua/LuaScriptState.h"

#include <cstdio>
#include <cassert>

using namespace mession::script::lua;

static void TestCreateEmpty()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaMap::Install(L);

    int RC = luaL_dostring(L, "return Mession.Map.new():size()");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -1) == 0);
    lua_pop(L, 1);
    std::printf("ok: TestCreateEmpty\n");
}

static void TestSetGet()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaMap::Install(L);

    int RC = luaL_dostring(L,
        "local m = Mession.Map.new()\n"
        "m:set('name', 'goblin')\n"
        "m:set('hp', 50)\n"
        "return m:get('name'), m:get('hp'), m:get('missing')");
    assert(RC == 0);
    size_t Len = 0;
    const char* P = lua_tolstring(L, -3, &Len);
    assert(P && MString(P, Len) == MString("goblin"));
    assert((int)lua_tointeger(L, -2) == 50);
    assert(lua_isnil(L, -1));
    lua_pop(L, 3);
    std::printf("ok: TestSetGet\n");
}

static void TestHasRemove()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaMap::Install(L);

    int RC = luaL_dostring(L,
        "local m = Mession.Map.new()\n"
        "m:set('a', 1); m:set('b', 2)\n"
        "local r1 = m:has('a')\n"
        "local v  = m:remove('a')\n"
        "local r2 = m:has('a')\n"
        "return r1, v, r2, m:size()");
    assert(RC == 0);
    assert(lua_toboolean(L, -4) == 1);
    assert((int)lua_tointeger(L, -3) == 1);
    assert(lua_toboolean(L, -2) == 0);
    assert((int)lua_tointeger(L, -1) == 1);
    lua_pop(L, 4);
    std::printf("ok: TestHasRemove\n");
}

static void TestKeysValues()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaMap::Install(L);

    int RC = luaL_dostring(L,
        "local m = Mession.Map.new()\n"
        "m:set(1, 'one'); m:set(2, 'two'); m:set(3, 'three')\n"
        "local keys = m:keys()\n"
        "local vals = m:values()\n"
        "return #keys, #vals");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -2) == 3);
    assert((int)lua_tointeger(L, -1) == 3);
    lua_pop(L, 2);
    std::printf("ok: TestKeysValues\n");
}

static void TestClear()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaMap::Install(L);

    int RC = luaL_dostring(L,
        "local m = Mession.Map.new()\n"
        "m:set('a', 1); m:set('b', 2); m:set('c', 3)\n"
        "m:clear()\n"
        "return m:size(), m:has('a')");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -2) == 0);
    assert(lua_toboolean(L, -1) == 0);
    lua_pop(L, 2);
    std::printf("ok: TestClear\n");
}

static void TestPairs()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaMap::Install(L);

    int RC = luaL_dostring(L,
        "local m = Mession.Map.new()\n"
        "m:set('a', 10); m:set('b', 20); m:set('c', 30)\n"
        "local sum = 0\n"
        "for k, v in pairs(m) do sum = sum + v end\n"
        "return sum");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -1) == 60);
    lua_pop(L, 1);
    std::printf("ok: TestPairs\n");
}

static void TestGC()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaMap::Install(L);

    int RC = luaL_dostring(L,
        "local m = Mession.Map.new()\n"
        "m:set('k1', 'v1'); m:set('k2', 'v2')\n"
        "m = nil\n"
        "collectgarbage()\n"
        "collectgarbage()\n"
        "return 1");
    assert(RC == 0);
    lua_pop(L, 1);
    std::printf("ok: TestGC\n");
}

int main()
{
    TestCreateEmpty();
    TestSetGet();
    TestHasRemove();
    TestKeysValues();
    TestClear();
    TestPairs();
    TestGC();
    std::printf("ALL OK\n");
    return 0;
}