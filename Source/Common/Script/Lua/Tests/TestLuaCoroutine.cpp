/**
 * @file TestLuaCoroutine.cpp
 * @brief 验证协程基础行为与 MLuaCoroutineBridge 的协程状态判定。
 */
#include "Common/Script/Lua/LuaCoroutineBridge.h"
#include "Common/Script/Lua/LuaScriptState.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

/** @brief TestCoroutineBasic - LoadBuffer 执行后,全局 co 应是 thread 类型 */
static void TestCoroutineBasic() {
    mession::script::lua::MLuaScriptState State;
    lua_State*                            L = State.GetLuaState();

    const char* Code = "co = coroutine.create(function () "
                       "    return 42 "
                       "end) "
                       "return 'ok'";
    MString     Err  = State.LoadBuffer("test_co", Code, strlen(Code));
    assert(Err.empty());

    lua_getglobal(L, "co");
    bool IsThread = lua_type(L, -1) == LUA_TTHREAD;
    lua_pop(L, 1);
    assert(IsThread);
    std::printf("ok: TestCoroutineBasic\n");
}

/**
 * @brief TestCoroutineResumeDirect - 直接 resume 无 yield 协程,应得 LUA_OK 且结果为 123。
 */
static void TestCoroutineResumeDirect() {
    // 无 yield 的协程:单次 resume 即返回 LUA_OK,栈顶就是 return 值。
    mession::script::lua::MLuaScriptState State;
    lua_State*                            L = State.GetLuaState();

    const char* Code = "co = coroutine.create(function () "
                       "    return 100 + 23 "
                       "end) "
                       "return 'ok'";
    MString     Err  = State.LoadBuffer("test_co_resume", Code, strlen(Code));
    assert(Err.empty());

    lua_getglobal(L, "co");
    lua_State* Co = lua_tothread(L, -1);
    lua_pop(L, 1);

    int NOut = 0;
    int RC   = lua_resume(Co, nullptr, 0, &NOut);
    if (RC != LUA_OK) {
        std::printf("FAIL: resume RC=%d err=%s\n", RC, lua_tostring(Co, -1));
        return;
    }
    int64 Got = (int64)lua_tointeger(Co, -1);
    lua_pop(Co, 1);
    if (Got != 123) {
        std::printf("FAIL: Got=%lld expected 123\n", (long long)Got);
        return;
    }
    std::printf("ok: TestCoroutineResumeDirect (Got=%lld)\n", (long long)Got);
}

/**
 * @brief TestCoroutineBridgeStatus - IsCoroutineYielded 在非协程上下文中应返回 false。
 */
static void TestCoroutineBridgeStatus() {
    mession::script::lua::MLuaScriptState State;
    lua_State*                            L = State.GetLuaState();

    bool Yielded = mession::script::lua::MLuaCoroutineBridge::IsCoroutineYielded(L);
    assert(!Yielded);
    std::printf("ok: TestCoroutineBridgeStatus\n");
}

/**
 * @brief main - 依次执行 3 个用例。
 * @return 恒为 0(断言失败会直接 abort)
 */
int main() {
    TestCoroutineBasic();
    TestCoroutineResumeDirect();
    TestCoroutineBridgeStatus();
    std::printf("ALL OK\n");
    return 0;
}