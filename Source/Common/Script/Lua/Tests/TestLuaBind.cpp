/**
 * @file TestLuaBind.cpp
 * @brief 验证 M.InvokeStatic 经 C++ 反射 NativeInvoke 的实参 / 返回值往返。
 *
 * @detail 本测试不再手写注册:
 *   - 测试类声明在 Source/Common/Script/Lua/MTestScriptBindClass.h(MCLASS + MFUNCTION);
 *   - MHeaderTool 解析后 emit MTestScriptBindClass.mgenerated.cpp/.h/.lua/.d.tl;
 *   - generated .cpp 于静态初始化时调 MTestScriptBindClass::StaticClass(),
 *     由 CreateNativeFunction 注册 NativeInvoke;
 *   - 本测试只需链接 generated obj,再调 StaticClass() 拿到 MClass*。
 */

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

/** @brief TestStaticAddInt - InvokeStatic("Add",{2,3}) 应返回 5 */
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

/** @brief TestStaticScaleNumber - InvokeStatic("Scale",{2.5}) 应返回 double 6.25 */
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

/** @brief TestStaticEchoString - InvokeStatic("Echo",{"hi"}) 应返回 "hi!"(MString) */
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

/** @brief TestStaticVoidReturn - void 返回的方法不压栈,后续 Lua 语句仍正常执行 */
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

/**
 * @brief main - 依次执行 4 个用例。
 * @return 恒为 0(断言失败会直接 abort)
 */
int main() {
    // StaticClass() 在静态初始化阶段已被 mht 生成的 SAutoRegisterClass_* 调用过,
    // 此处再调一次只是验证它幂等。
    (void)MTestScriptBindClass::StaticClass();

    TestStaticAddInt();
    TestStaticScaleNumber();
    TestStaticEchoString();
    TestStaticVoidReturn();
    std::printf("ALL OK\n");
    return 0;
}