#include "Common/Script/Lua/LuaScriptState.h"
#include "Common/Script/Lua/LuaCoroutineBridge.h"

#include <cstdio>
#include <cassert>
#include <cstring>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

static int LuaCoroutineResume(lua_State* L)
{
    lua_getglobal(L, "co");
    if (lua_type(L, -1) != LUA_TTHREAD)
    {
        return luaL_error(L, "expected coroutine");
    }
    lua_State* Co = lua_tothread(L, -1);
    lua_pop(L, 1);

    lua_pushinteger(Co, 42);
    int RC = lua_resume(Co, nullptr, 1);
    if (RC != LUA_OK)
    {
        return luaL_error(L, "resume failed: %s", lua_tostring(Co, -1));
    }
    int64 Got = (int64)lua_tointeger(Co, -1);
    lua_pop(Co, 1);
    lua_pushinteger(L, Got);
    return 1;
}

static void TestCoroutineYieldResume()
{
    mession::script::lua::MLuaScriptState State;
    lua_State* L = State.GetLuaState();

    const char* Code =
        "co = coroutine.create(function () "
        "    coroutine.yield() "
        "    return 99 "
        "end) "
        "return 'ok'";
    MString Err = State.LoadBuffer("test_co", Code, strlen(Code));
    assert(Err.empty());

    lua_pushcfunction(L, LuaCoroutineResume);
    int RC = lua_pcall(L, 0, 1, 0);
    assert(RC == 0);
    int64 Got = (int64)lua_tointeger(L, -1);
    lua_pop(L, 1);
    assert(Got == 42);
    std::printf("ok: TestCoroutineYieldResume\n");
}

int main()
{
    TestCoroutineYieldResume();
    std::printf("ALL OK\n");
    return 0;
}