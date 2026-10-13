/**
 * @file TestLuaTypeBridge.cpp
 * @brief 验证 LuaTypeBridge 的标量 push / pop 往返与类型不匹配的错误路径。
 */
#include "Common/Runtime/MLib.h"
#include "Common/Script/Lua/LuaScriptState.h"
#include "Common/Script/Lua/LuaTypeBridge.h"

#include <cassert>
#include <cstdio>

/** @brief TestPushPopInteger - PushInteger / PopInteger 往返值为 42 */
static void TestPushPopInteger() {
    mession::script::lua::MLuaScriptState State;
    mession::script::lua::PushInteger(State.GetLuaState(), 42);
    auto Got = mession::script::lua::PopInteger(State.GetLuaState(), -1);
    assert(Got.IsOk());
    assert(Got.GetValue() == 42);
    std::printf("ok: TestPushPopInteger\n");
}

/** @brief TestPushPopString - PushString / PopString 往返值为 "hello" */
static void TestPushPopString() {
    mession::script::lua::MLuaScriptState State;
    MString                               S = "hello";
    mession::script::lua::PushString(State.GetLuaState(), S);
    auto Got = mession::script::lua::PopString(State.GetLuaState(), -1);
    assert(Got.IsOk());
    assert(Got.GetValue() == "hello");
    std::printf("ok: TestPushPopString\n");
}

/** @brief TestPushPopBool - PushBoolean / PopBoolean 往返值为 true */
static void TestPushPopBool() {
    mession::script::lua::MLuaScriptState State;
    mession::script::lua::PushBoolean(State.GetLuaState(), true);
    auto Got = mession::script::lua::PopBoolean(State.GetLuaState(), -1);
    assert(Got.IsOk());
    assert(Got.GetValue() == true);
    std::printf("ok: TestPushPopBool\n");
}

/** @brief TestTypeMismatch - 压入 integer 后按 string 读取应返回 Err */
static void TestTypeMismatch() {
    mession::script::lua::MLuaScriptState State;
    mession::script::lua::PushInteger(State.GetLuaState(), 1);
    auto Got = mession::script::lua::PopString(State.GetLuaState(), -1);
    assert(Got.IsErr());
    std::printf("ok: TestTypeMismatch\n");
}

/**
 * @brief main - 依次执行 4 个用例。
 * @return 恒为 0(断言失败会直接 abort)
 */
int main() {
    TestPushPopInteger();
    TestPushPopString();
    TestPushPopBool();
    TestTypeMismatch();
    std::printf("ALL OK\n");
    return 0;
}