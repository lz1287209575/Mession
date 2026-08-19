#include "Common/Script/Lua/MLuaVector.h"
#include "Common/Script/Lua/LuaScriptState.h"

#include <cstdio>
#include <cassert>

using namespace mession::script::lua;

static void TestCreateEmpty()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L, "return Mession.Vector.new():size()");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -1) == 0);
    lua_pop(L, 1);
    std::printf("ok: TestCreateEmpty\n");
}

static void TestPushPop()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L,
        "local v = Mession.Vector.new()\n"
        "v:push(10); v:push(20); v:push(30)\n"
        "local last = v:pop()\n"
        "return last, v:size()");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -2) == 30);
    assert((int)lua_tointeger(L, -1) == 2);
    lua_pop(L, 2);
    std::printf("ok: TestPushPop\n");
}

static void TestGetSet()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L,
        "local v = Mession.Vector.new(3, 0)\n"
        "v:set(2, 99)\n"
        "return v:get(1), v:get(2), v:get(3)");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -3) == 0);
    assert((int)lua_tointeger(L, -2) == 99);
    assert((int)lua_tointeger(L, -1) == 0);
    lua_pop(L, 3);
    std::printf("ok: TestGetSet\n");
}

static void TestInsertRemove()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L,
        "local v = Mession.Vector.new()\n"
        "v:push(1); v:push(3)\n"
        "v:insert(2, 99)\n"
        "local removed = v:remove(2)\n"
        "return v:size(), removed, v:get(1), v:get(2)");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -4) == 3);
    assert((int)lua_tointeger(L, -3) == 99);
    assert((int)lua_tointeger(L, -2) == 1);
    assert((int)lua_tointeger(L, -1) == 3);
    lua_pop(L, 4);
    std::printf("ok: TestInsertRemove\n");
}

static void TestBoundary()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L,
        "local v = Mession.Vector.new(2, 0)\n"
        "local ok, err = pcall(v.set, v, 100, 99)\n"
        "return ok, err");
    assert(RC == 0);
    assert(lua_toboolean(L, -2) == 0);
    lua_pop(L, 2);
    std::printf("ok: TestBoundary\n");
}

static void TestEq()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L,
        "local v1 = Mession.Vector.new(3, 1)\n"
        "local v2 = Mession.Vector.new(3, 1)\n"
        "return v1 == v2, v1:get(1) == v2:get(1)");
    assert(RC == 0);
    assert(lua_toboolean(L, -2) == 1);
    assert(lua_toboolean(L, -1) == 1);
    lua_pop(L, 2);
    std::printf("ok: TestEq\n");
}

static void TestIpairs()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L,
        "local v = Mession.Vector.new()\n"
        "v:push(10); v:push(20); v:push(30)\n"
        "local sum = 0\n"
        "for i, v in ipairs(v) do sum = sum + v end\n"
        "return sum");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -1) == 60);
    lua_pop(L, 1);
    std::printf("ok: TestIpairs\n");
}

static void TestClone()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L,
        "local v = Mession.Vector.new()\n"
        "v:push(10); v:push(20)\n"
        "local c = v:clone()\n"
        "c:push(30)\n"
        "return v:size(), c:size()");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -2) == 2);
    assert((int)lua_tointeger(L, -1) == 3);
    lua_pop(L, 2);
    std::printf("ok: TestClone\n");
}

static void TestGC()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L,
        "local v = Mession.Vector.new()\n"
        "v:push(1); v:push(2)\n"
        "v = nil\n"
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
    TestPushPop();
    TestGetSet();
    TestInsertRemove();
    TestBoundary();
    TestEq();
    TestIpairs();
    TestClone();
    TestGC();
    std::printf("ALL OK\n");
    return 0;
}