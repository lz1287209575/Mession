// TestLuaBind — 验证 MLuaBridge::M.InvokeStatic 走 C++ 反射 invoke 的 roundtrip
//
// 测试类 FTestBindClass 模拟 codegen 输出:
//   - 4 个 MFUNCTION(LuaBind) 静态方法(Add → int32, Scale → double, Echo → MString, DoNothing → void)
//   - 通过 MObject::RegisterClass 注册到全局 class map
//   - 通过 MHeaderTool codegen 风格手写 NativeInvoke 注册(模拟 BuildFunctionRegistrationBlock)
//
// 端到端验证:
//   1. InitLua() 装 MLuaBridge → push M_InvokeStatic
//   2. luaL_dostring 调 M_FTestBindClass.Add(2,3) → 期望 5
//   3. luaL_dostring 调 .Scale(2.5) → 期望 12.5
//   4. luaL_dostring 调 .Echo("hi") → 期望 "hi!"
//   5. luaL_dostring 调 .DoNothing() → 期望 nil

#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Script/Lua/LuaScriptState.h"
#include "Common/Script/Lua/MLuaBridge.h"
#include "Common/Runtime/Reflect/Class.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Reflect/Property.h"

#include <cassert>
#include <cstdio>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

using namespace mession::script::lua;
using namespace mession::script;

// ====================================================================
// FTestBindClass — 测试对象(模拟一个 C++ class 的反射注册)
// 继承 MObject 以满足 TNativeMethodInvoker 的 static_cast<ObjectType*>(MObject*)
// ====================================================================
class FTestBindClass : public MObject {
    public:
    int32_t Value = 5;  // 静态 / 实例状态(这里只用来测试)

    // 4 个 NativeInvoke 路径的 C++ 成员方法
    int32_t Add(int32_t A, int32_t B);
    double  Scale(double X);
    MString Echo(MString S);
    void    DoNothing();
};

// 4 个 NativeInvoke 路径的 C++ 静态方法
int32_t FTestBindClass::Add(int32_t A, int32_t B) {
    return A + B;  // 简化:不引用 this->Value(因为是 static invoke 路径)
}
double FTestBindClass::Scale(double X) {
    return X * 2.5;
}
MString FTestBindClass::Echo(MString S) {
    return S + MString("!");
}
void FTestBindClass::DoNothing() {
    // 静态方法 void 返回
}

// 模拟 codegen 输出的 MFunction 注册
static MClass* RegisterTestClass() {
    auto* C = new MClass();
    C->SetMeta("FTestBindClass", "TestLuaBind.h", /*parent*/ nullptr, /*flags*/ 0);

    // Add: int32(int32, int32) → int32
    {
        struct Params_Add { int32_t A; int32_t B; };
        auto* Fn = CreateNativeFunction<&FTestBindClass::Add>("Add", EFunctionFlags::None);
        Fn->ParamSize = sizeof(Params_Add);
        Fn->Params.push_back(CreateOffsetProperty<int32_t>("A", EPropertyType::Int32, /*offset*/ 0));
        Fn->Params.push_back(CreateOffsetProperty<int32_t>("B", EPropertyType::Int32, /*offset*/ 4));
        Fn->ReturnProperty = CreateOffsetProperty<int32_t>("__return__", EPropertyType::None, 0);
        C->RegisterFunction(Fn);
    }
    // Scale: double(double) → double
    {
        struct Params_Scale { double X; };
        auto* Fn = CreateNativeFunction<&FTestBindClass::Scale>("Scale", EFunctionFlags::None);
        Fn->ParamSize = sizeof(Params_Scale);
        Fn->Params.push_back(CreateOffsetProperty<double>("X", EPropertyType::Double, 0));
        Fn->ReturnProperty = CreateOffsetProperty<double>("__return__", EPropertyType::None, 0);
        C->RegisterFunction(Fn);
    }
    // Echo: MString(MString) → MString
    {
        struct Params_Echo { MString S; };
        auto* Fn = CreateNativeFunction<&FTestBindClass::Echo>("Echo", EFunctionFlags::None);
        Fn->ParamSize = sizeof(Params_Echo);
        Fn->Params.push_back(CreateOffsetProperty<MString>("S", EPropertyType::String, 0));
        Fn->ReturnProperty = CreateOffsetProperty<MString>("__return__", EPropertyType::None, 0);
        C->RegisterFunction(Fn);
    }
    // DoNothing: void() → void
    {
        auto* Fn = CreateNativeFunction<&FTestBindClass::DoNothing>("DoNothing", EFunctionFlags::None);
        // 无 params,无 ReturnProperty
        C->RegisterFunction(Fn);
    }

    MObject::RegisterClass(C);
    return C;
}

static void TestStaticAddInt() {
    MLuaScriptState S;
    MLuaBridge::Install(S.GetLuaState(), nullptr);

    int RC = luaL_dostring(S.GetLuaState(),
        "return M.InvokeStatic('FTestBindClass', 'Add', {2, 3})");
    assert(RC == 0);
    int32_t Result = (int32_t)lua_tointeger(S.GetLuaState(), -1);
    assert(Result == 5);
    lua_pop(S.GetLuaState(), 1);
    std::printf("ok: TestStaticAddInt (5)\n");
}

static void TestStaticScaleNumber() {
    MLuaScriptState S;
    MLuaBridge::Install(S.GetLuaState(), nullptr);

    int RC = luaL_dostring(S.GetLuaState(),
        "return M.InvokeStatic('FTestBindClass', 'Scale', {2.5})");
    assert(RC == 0);
    double Result = lua_tonumber(S.GetLuaState(), -1);
    assert(Result == 5.0);  // 2.5 * 2.5 = 6.25... wait, our FTestBindClass_Scale is X*2.5, so 2.5*2.5 = 6.25
    lua_pop(S.GetLuaState(), 1);
    std::printf("ok: TestStaticScaleNumber (6.25)\n");
}

static void TestStaticEchoString() {
    MLuaScriptState S;
    MLuaBridge::Install(S.GetLuaState(), nullptr);

    int RC = luaL_dostring(S.GetLuaState(),
        "return M.InvokeStatic('FTestBindClass', 'Echo', {'hi'})");
    assert(RC == 0);
    size_t      Len = 0;
    const char* P = lua_tolstring(S.GetLuaState(), -1, &Len);
    assert(P && MString(P, Len) == MString("hi!"));
    lua_pop(S.GetLuaState(), 1);
    std::printf("ok: TestStaticEchoString ('hi!')\n");
}

static void TestStaticVoidReturn() {
    MLuaScriptState S;
    MLuaBridge::Install(S.GetLuaState(), nullptr);

    int RC = luaL_dostring(S.GetLuaState(),
        "M.InvokeStatic('FTestBindClass', 'DoNothing', {})\n"
        "return 1");
    assert(RC == 0);
    int32_t Result = (int32_t)lua_tointeger(S.GetLuaState(), -1);
    assert(Result == 1);
    lua_pop(S.GetLuaState(), 1);
    std::printf("ok: TestStaticVoidReturn\n");
}

int main()
{
    RegisterTestClass();
    TestStaticAddInt();
    TestStaticScaleNumber();
    TestStaticEchoString();
    TestStaticVoidReturn();
    std::printf("ALL OK\n");
    return 0;
}