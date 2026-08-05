#include "Common/Script/Lua/LuaTypeBridge.h"
#include "Common/Script/Lua/LuaScriptState.h"
#include "Common/Runtime/MLib.h"

#include <cstdio>
#include <cassert>

static void TestPushPopInteger() {
    mession::script::lua::MLuaScriptState State;
    mession::script::lua::PushInteger(State.GetLuaState(), 42);
    auto Got = mession::script::lua::PopInteger(State.GetLuaState(), -1);
    assert(Got.IsOk());
    assert(Got.GetValue() == 42);
    std::printf("ok: TestPushPopInteger\n");
}

static void TestPushPopString() {
    mession::script::lua::MLuaScriptState State;
    MString S = "hello";
    mession::script::lua::PushString(State.GetLuaState(), S);
    auto Got = mession::script::lua::PopString(State.GetLuaState(), -1);
    assert(Got.IsOk());
    assert(Got.GetValue() == "hello");
    std::printf("ok: TestPushPopString\n");
}

static void TestPushPopBool() {
    mession::script::lua::MLuaScriptState State;
    mession::script::lua::PushBoolean(State.GetLuaState(), true);
    auto Got = mession::script::lua::PopBoolean(State.GetLuaState(), -1);
    assert(Got.IsOk());
    assert(Got.GetValue() == true);
    std::printf("ok: TestPushPopBool\n");
}

static void TestTypeMismatch() {
    mession::script::lua::MLuaScriptState State;
    mession::script::lua::PushInteger(State.GetLuaState(), 1);
    auto Got = mession::script::lua::PopString(State.GetLuaState(), -1);
    assert(Got.IsErr());
    std::printf("ok: TestTypeMismatch\n");
}

int main() {
    TestPushPopInteger();
    TestPushPopString();
    TestPushPopBool();
    TestTypeMismatch();
    std::printf("ALL OK\n");
    return 0;
}