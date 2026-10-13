/**
 * @file TestLuaLog.cpp
 * @brief 验证 Mession.Log.* 的注册、调用与类型校验。
 */
#include "Common/Script/Lua/LuaScriptState.h"
#include "Common/Script/Lua/MLuaLog.h"

#include <cassert>
#include <cstdio>

using namespace mession::script::lua;

/** @brief TestLogInstall - Mession.Log 及其 5 个入口都应是 function */
static void TestLogInstall() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaLog::Install(L);

    int RC = luaL_dostring(L, "return type(Mession.Log), type(Mession.Log.info), "
                              "type(Mession.Log.warn), type(Mession.Log.error), "
                              "type(Mession.Log.debug), type(Mession.Log.fatal)");
    assert(RC == 0);
    for (int i = -5; i <= 0; ++i) {
        size_t      Len = 0;
        const char* P   = lua_tolstring(L, i, &Len);
        assert(P && MString(P, Len) == MString("function"));
    }
    lua_pop(L, 6);
    std::printf("ok: TestLogInstall\n");
}

/** @brief TestLogCall - 各级别日志调用都不应抛错 */
static void TestLogCall() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaLog::Install(L);

    // 调用不应该抛错(MLog 默认 sink 会输出到 console,我们只验证 call OK)
    int RC = luaL_dostring(L, "Mession.Log.info('hello info')\n"
                              "Mession.Log.warn('hello warn')\n"
                              "Mession.Log.error('hello error')\n"
                              "Mession.Log.debug('hello debug')\n"
                              "return 1");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -1) == 1);
    lua_pop(L, 1);
    std::printf("ok: TestLogCall\n");
}

/** @brief TestLogTypeError - 非 string 的 msg 应经 pcall 捕获为错误 */
static void TestLogTypeError() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaLog::Install(L);

    // 非 string msg 应抛错
    int RC = luaL_dostring(L, "local ok, err = pcall(Mession.Log.info, 123)\n"
                              "return ok, err");
    assert(RC == 0);
    assert(lua_toboolean(L, -2) == 0);
    lua_pop(L, 2);
    std::printf("ok: TestLogTypeError\n");
}

/**
 * @brief main - 依次执行 3 个用例。
 * @return 恒为 0(断言失败会直接 abort)
 */
int main() {
    TestLogInstall();
    TestLogCall();
    TestLogTypeError();
    std::printf("ALL OK\n");
    return 0;
}