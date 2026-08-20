#include "Common/Script/Abstract/EReloadMode.h"
#include "Common/Script/Abstract/EReloadResult.h"
#include "Common/Script/Abstract/EScriptLanguage.h"
#include "Common/Script/Abstract/IScriptEngine.h"
#include "Common/Script/Abstract/IScriptModule.h"
#include "Common/Script/Abstract/IScriptRepl.h"
#include "Common/Script/Abstract/SScriptEngineConfig.h"
#include "Common/Script/Abstract/ScriptErrorCodes.h"
#include "Common/Script/Abstract/TVariant.h"
#include "Common/Runtime/Reflect/Reflection.h"

#include <cassert>
#include <cstdio>

using namespace mession::script;

static void TestEScriptLanguageNames() {
    assert(MString(ScriptLanguageName(EScriptLanguage::Lua)) == "Lua");
    assert(MString(ScriptLanguageName(EScriptLanguage::Python)) == "Python");
    assert(MString(ScriptLanguageName(EScriptLanguage::TypeScript)) == "TypeScript");
    assert(MString(ScriptLanguageName(EScriptLanguage::CSharp)) == "CSharp");
    assert(MString(ScriptLanguageName(EScriptLanguage::CppRepl)) == "CppRepl");
    std::printf("ok: TestEScriptLanguageNames\n");
}

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

static void TestSScriptArgsCtor() {
    TVariant Args[2];
    Args[0] = TVariant::MakeInt(1);
    Args[1] = TVariant::MakeString("x");
    SScriptArgs SA(Args, 2);
    assert(SA.Values == Args);
    assert(SA.Count == 2);
    std::printf("ok: TestSScriptArgsCtor\n");
}

static void TestScriptErrorCodes() {
    FScriptError E(ScriptErrorCodes::kLuaRuntime, "attempt to index a nil value");
    MString      S = ToErrorString(E);
    assert(S.find("[lua_runtime_error]") != MString::npos);
    assert(S.find("attempt to index a nil value") != MString::npos);
    std::printf("ok: TestScriptErrorCodes\n");
}

static void TestEnumReflection() {
    // namespace 级 scoped enum 反射注册(A3 修复前是 no-op nullptr)
    MEnum* Enum = MObject::FindEnum("EScriptLanguage");
    assert(Enum != nullptr);
    assert(Enum->GetValues().size() >= 4);
    std::printf("ok: TestEnumReflection (FindEnum EScriptLanguage, %zu values)\n", Enum->GetValues().size());
}

int main() {
    TestEScriptLanguageNames();
    TestTVariantRoundtrip();
    TestSScriptArgsCtor();
    TestScriptErrorCodes();
    TestEnumReflection();
    std::printf("ALL OK\n");
    return 0;
}