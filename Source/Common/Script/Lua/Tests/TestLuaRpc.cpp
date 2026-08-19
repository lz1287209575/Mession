#include "Common/Script/Lua/MLuaRpc.h"
#include "Common/Script/Lua/LuaScriptState.h"

#include <cstdio>
#include <cassert>

using namespace mession::script::lua;

// 测试用的 mock bridge — 简单地取 Lua 全局函数并调用
class FMockBridge : public IScriptRpcBridge {
public:
    int CallGlobal(lua_State* L, const char* GlobalName, int ArgStart, int ArgEnd) override
    {
        lua_getglobal(L, GlobalName);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            lua_pushnil(L);
            lua_pushliteral(L, "not_a_function");
            return 2;
        }
        int NumArgs = ArgEnd - ArgStart + 1;
        for (int i = ArgStart; i <= ArgEnd; ++i) {
            lua_pushvalue(L, i);
        }
        if (lua_pcall(L, NumArgs, 0, 0) != LUA_OK) {
            lua_pushnil(L);
            lua_pushvalue(L, -2); // err msg
            return 2;
        }
        return 0;
    }
};

static void TestRpcCall()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    FMockBridge Mock;
    MLuaRpc::Install(L, &Mock);

    int RC = luaL_dostring(L,
        "local function greet(name) return 'hello ' .. name end\n"
        "return Mession.RPC.call('greet', 'lua')");
    assert(RC == 0);
    size_t      Len = 0;
    const char* P   = lua_tolstring(L, -1, &Len);
    assert(P && MString(P, Len) == MString("hello lua"));
    lua_pop(L, 1);
    std::printf("ok: TestRpcCall\n");
}

static void TestRpcNotFound()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    FMockBridge Mock;
    MLuaRpc::Install(L, &Mock);

    int RC = luaL_dostring(L,
        "local v, err = Mession.RPC.call('does_not_exist', 1, 2)\n"
        "return v, err");
    assert(RC == 0);
    assert(lua_isnil(L, -2));
    size_t      Len = 0;
    const char* P   = lua_tolstring(L, -1, &Len);
    assert(P && MString(P, Len) == MString("not_a_function"));
    lua_pop(L, 2);
    std::printf("ok: TestRpcNotFound\n");
}

static void TestTimeNow()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    FMockBridge Mock;
    MLuaRpc::Install(L, &Mock);

    int RC = luaL_dostring(L,
        "local s = Mession.Time.now()\n"
        "local ms = Mession.Time.nowMs()\n"
        "return type(s), type(ms), ms > 0");
    assert(RC == 0);
    size_t      Len = 0;
    const char* T1 = lua_tolstring(L, -3, &Len);
    const char* T2 = lua_tolstring(L, -2, &Len);
    assert(T1 && MString(T1, std::strlen(T1)) == MString("number"));
    assert(T2 && MString(T2, std::strlen(T2)) == MString("number"));
    assert(lua_toboolean(L, -1) == 1);
    lua_pop(L, 3);
    std::printf("ok: TestTimeNow\n");
}

static void TestIdUniqueness()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    FMockBridge Mock;
    MLuaRpc::Install(L, &Mock);

    int RC = luaL_dostring(L,
        "local a = Mession.Id.new()\n"
        "local b = Mession.Id.new()\n"
        "return a, b, a < b");
    assert(RC == 0);
    int64 A = (int64)lua_tointeger(L, -3);
    int64 B = (int64)lua_tointeger(L, -2);
    assert(A < B);
    assert(lua_toboolean(L, -1) == 1);
    lua_pop(L, 3);
    std::printf("ok: TestIdUniqueness\n");
}

static void TestSleep()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    FMockBridge Mock;
    MLuaRpc::Install(L, &Mock);

    // 1ms sleep 应该能跑通(不验证时间精确)
    int RC = luaL_dostring(L,
        "Mession.Time.sleepMs(1)\n"
        "return 1");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -1) == 1);
    lua_pop(L, 1);
    std::printf("ok: TestSleep\n");
}

int main()
{
    TestRpcCall();
    TestRpcNotFound();
    TestTimeNow();
    TestIdUniqueness();
    TestSleep();
    std::printf("ALL OK\n");
    return 0;
}