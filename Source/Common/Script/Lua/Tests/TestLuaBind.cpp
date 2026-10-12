// TestLuaBind — 验证 MLuaBridge::M.InvokeStatic 走 C++ 反射 invoke 的 roundtrip
//
// 这个 test **不再手写 Register**:
// - 测试类在 Source/Common/Script/Lua/MTestScriptBindClass.h(MCLASS + MFUNCTION(ScriptBind))
// - mht 解析 emit MTestScriptBindClass.mgenerated.cpp/.h/.lua/.d.tl
// - MTestScriptBindClass.mgenerated.cpp 静态 init 时调 MTestScriptBindClass::StaticClass()
//   → CreateNativeFunction 注册 NativeInvoke
// - TestLuaBind 链接 generated obj,只调 MTestScriptBindClass::StaticClass() 拿 MClass*

#include "Common/Runtime/Reflect/Class.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Script/Lua/LuaScriptState.h"
#include "Common/Script/Lua/MLuaBridge.h"
#include "Common/Script/Lua/MTestScriptBindClass.h" // 真 MHeaderTool 注解 + generated.h(.mgenerated.h 通过 .h include)

#include <cassert>
#include <cstdio>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

using namespace mession::script::lua;
using namespace mession::script;

static void TestStaticAddInt() {
    MLuaScriptState S;
    MLuaBridge::Install(S.GetLuaState(), nullptr);

    // 通过 StaticClass() 拿 mht 自动注册的 MClass*,不是手写 Register
    MClass* Cls = MTestScriptBindClass::StaticClass();
    assert(Cls != nullptr);
    assert(MObject::FindClass("MTestScriptBindClass") == Cls);

    int RC = luaL_dostring(S.GetLuaState(), "return M.InvokeStatic('MTestScriptBindClass', 'Add', {2, 3})");
    assert(RC == 0);
    int32_t Result = (int32_t)lua_tointeger(S.GetLuaState(), -1);
    assert(Result == 5);
    lua_pop(S.GetLuaState(), 1);
    std::printf("ok: TestStaticAddInt (5)\n");
}

static void TestStaticScaleNumber() {
    MLuaScriptState S;
    MLuaBridge::Install(S.GetLuaState(), nullptr);

    int RC = luaL_dostring(S.GetLuaState(), "return M.InvokeStatic('MTestScriptBindClass', 'Scale', {2.5})");
    assert(RC == 0);
    double Result = lua_tonumber(S.GetLuaState(), -1);
    assert(Result == 6.25); // 2.5 * 2.5
    lua_pop(S.GetLuaState(), 1);
    std::printf("ok: TestStaticScaleNumber (6.25)\n");
}

static void TestStaticEchoString() {
    MLuaScriptState S;
    MLuaBridge::Install(S.GetLuaState(), nullptr);

    int RC = luaL_dostring(S.GetLuaState(), "return M.InvokeStatic('MTestScriptBindClass', 'Echo', {'hi'})");
    assert(RC == 0);
    size_t      Len = 0;
    const char* P   = lua_tolstring(S.GetLuaState(), -1, &Len);
    assert(P && MString(P, Len) == MString("hi!"));
    lua_pop(S.GetLuaState(), 1);
    std::printf("ok: TestStaticEchoString ('hi!')\n");
}

static void TestStaticVoidReturn() {
    MLuaScriptState S;
    MLuaBridge::Install(S.GetLuaState(), nullptr);

    int RC = luaL_dostring(S.GetLuaState(), "M.InvokeStatic('MTestScriptBindClass', 'DoNothing', {})\n"
                                            "return 1");
    assert(RC == 0);
    int32_t Result = (int32_t)lua_tointeger(S.GetLuaState(), -1);
    assert(Result == 1);
    lua_pop(S.GetLuaState(), 1);
    std::printf("ok: TestStaticVoidReturn\n");
}

int main() {
    // MTestScriptBindClass::StaticClass() 在程序启动静态初始化时已被 mht 生成的
    // SAutoRegisterClass_MTestScriptBindClass 调用 — 此处调用只是验证它 idempotent
    (void)MTestScriptBindClass::StaticClass();

    TestStaticAddInt();
    TestStaticScaleNumber();
    TestStaticEchoString();
    TestStaticVoidReturn();
    std::printf("ALL OK\n");
    return 0;
}