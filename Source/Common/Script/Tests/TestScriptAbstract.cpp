/**
 * @file TestScriptAbstract.cpp
 * @brief 验证 Abstract 层基础类型:语言枚举名、TVariant、SScriptArgs、错误码与枚举反射。
 */
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Script/Abstract/EReloadMode.h"
#include "Common/Script/Abstract/EReloadResult.h"
#include "Common/Script/Abstract/EScriptLanguage.h"
#include "Common/Script/Abstract/IScriptEngine.h"
#include "Common/Script/Abstract/IScriptModule.h"
#include "Common/Script/Abstract/IScriptRepl.h"
#include "Common/Script/Abstract/SScriptEngineConfig.h"
#include "Common/Script/Abstract/ScriptErrorCodes.h"
#include "Common/Script/Abstract/TVariant.h"

#include <cassert>
#include <cstdio>

using namespace mession::script;

/** @brief TestEScriptLanguageNames - ScriptLanguageName 应返回各语言的规范名 */
static void TestEScriptLanguageNames() {
    assert(MString(ScriptLanguageName(EScriptLanguage::Lua)) == "Lua");
    assert(MString(ScriptLanguageName(EScriptLanguage::Python)) == "Python");
    assert(MString(ScriptLanguageName(EScriptLanguage::TypeScript)) == "TypeScript");
    assert(MString(ScriptLanguageName(EScriptLanguage::CSharp)) == "CSharp");
    assert(MString(ScriptLanguageName(EScriptLanguage::CppRepl)) == "CppRepl");
    std::printf("ok: TestEScriptLanguageNames\n");
}

/** @brief TestTVariantRoundtrip - Int / String 往返一致,类型不匹配时 AsBool 返回 Err */
static void TestTVariantRoundtrip() {
    TVariant V = TVariant::MakeInt(42);
    assert(V.GetType() == EVariantType::Int);
    auto Got = V.AsInt();
    assert(Got.IsOk());
    assert(Got.GetValue() == 42);

    TVariant S = TVariant::MakeString("hello");
    assert(S.GetType() == EVariantType::String);
    auto GotStr = S.AsString();
    assert(GotStr.IsOk());
    assert(GotStr.GetValue() == "hello");

    // 类型不匹配返回 Err
    TVariant Wrong   = TVariant::MakeInt(1);
    auto     BadBool = Wrong.AsBool();
    assert(BadBool.IsErr());
    std::printf("ok: TestTVariantRoundtrip\n");
}

/** @brief TestSScriptArgsCtor - SScriptArgs 直接持有传入的数组指针与个数 */
static void TestSScriptArgsCtor() {
    TVariant Args[2];
    Args[0] = TVariant::MakeInt(1);
    Args[1] = TVariant::MakeString("x");
    SScriptArgs SA(Args, 2);
    assert(SA.Values == Args);
    assert(SA.Count == 2);
    std::printf("ok: TestSScriptArgsCtor\n");
}

/** @brief TestScriptErrorCodes - ToErrorString 应同时含 "[code]" 前缀与原始消息 */
static void TestScriptErrorCodes() {
    FScriptError E(ScriptErrorCodes::kLuaRuntime, "attempt to index a nil value");
    MString      S = ToErrorString(E);
    assert(S.find("[lua_runtime_error]") != MString::npos);
    assert(S.find("attempt to index a nil value") != MString::npos);
    std::printf("ok: TestScriptErrorCodes\n");
}

/**
 * @brief TestEnumReflection - namespace 级 scoped enum 应已注册到反射系统。
 *
 * @detail 早期实现下 FindEnum("EScriptLanguage") 曾返回 nullptr。
 */
static void TestEnumReflection() {
    MEnum* Enum = MObject::FindEnum("EScriptLanguage");
    assert(Enum != nullptr);
    assert(Enum->GetValues().size() >= 4);
    std::printf("ok: TestEnumReflection (FindEnum EScriptLanguage, %zu values)\n", Enum->GetValues().size());
}

/**
 * @brief main - 依次执行 5 个用例。
 * @return 恒为 0(断言失败会直接 abort)
 */
int main() {
    TestEScriptLanguageNames();
    TestTVariantRoundtrip();
    TestSScriptArgsCtor();
    TestScriptErrorCodes();
    TestEnumReflection();
    std::printf("ALL OK\n");
    return 0;
}