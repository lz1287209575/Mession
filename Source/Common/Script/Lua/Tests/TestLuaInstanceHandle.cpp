/**
 * @file TestLuaInstanceHandle.cpp
 * @brief 验证 MLuaEngine 的实例句柄生命周期:创建、方法调用、错误路径与释放。
 */
#include "Common/Script/Abstract/TVariant.h"
#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Script/Lua/LuaScriptState.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace mession::script;
using namespace mession::script::lua;

namespace {
    MLuaEngine* GEngine = nullptr;

    /**
     * @brief SetupEnv - 把业务 Lua 代码加载进引擎内部 State。
     * @param Code 待加载的 Lua 源码
     */
    void SetupEnv(const char* Code) {
        MString Err = GEngine->LoadBufferIntoState("test", Code, strlen(Code));
        assert(Err.empty());
    }

    /** @brief MakeVarInt - 构造 Int 型 TVariant 实参 */
    TVariant MakeVarInt(int64 V) {
        return TVariant::MakeInt(V);
    }
    /** @brief MakeVarStr - 构造 String 型 TVariant 实参 */
    TVariant MakeVarStr(const char* S) {
        return TVariant::MakeString(MString(S));
    }
} // namespace

/** @brief TestBasicCreate - CreateInstanceByClassName 应返回有效句柄 */
static void TestBasicCreate() {
    SetupEnv("Monster = {}\n"
             "Monster.__index = Monster\n"
             "function Monster:new(name, hp) return setmetatable({name=name, hp=hp or 100}, Monster) end\n"
             "function Monster:take_damage(d) self.hp = self.hp - d; return self.hp end\n");

    TScriptArgs Args;
    TVariant    V0[] = {MakeVarStr("goblin"), MakeVarInt(100)};
    Args.Values      = V0;
    Args.Count       = 2;

    auto R = GEngine->CreateInstanceByClassName("Monster", Args);
    if (!R.IsOk()) {
        std::printf("ERR: %s\n", R.GetError().c_str());
        std::fflush(stdout);
    }
    assert(R.IsOk());
    TScriptInstanceHandle H = R.GetValue();
    assert(H.IsValid());
    assert(H.GetId() != TScriptInstanceHandle::InvalidId);
    std::printf("ok: TestBasicCreate\n");
    GEngine->ReleaseInstance(H);
}

/** @brief TestFallbackCall - 类无 :new 时经 __call 建实例;未知方法应返回 Err */
static void TestFallbackCall() {
    SetupEnv("Adder = {}\n"
             "setmetatable(Adder, {__call = function(_, a, b) return a + b end})\n");

    TScriptArgs Args;
    TVariant    V0[] = {MakeVarInt(3), MakeVarInt(4)};
    Args.Values      = V0;
    Args.Count       = 2;

    auto R = GEngine->CreateInstanceByClassName("Adder", Args);
    assert(R.IsOk());
    TScriptInstanceHandle H = R.GetValue();
    assert(H.IsValid());

    // Adder 没有 :new,fallback 到 __call
    auto M = GEngine->InvokeInstanceMethod(H, "nope", Args);
    assert(M.IsErr());
    std::printf("ok: TestFallbackCall\n");
    GEngine->ReleaseInstance(H);
}

/** @brief TestClassNotFound - 不存在的类名应返回含 "class_not_found" 的 Err */
static void TestClassNotFound() {
    TScriptArgs Args;
    auto        R = GEngine->CreateInstanceByClassName("NoSuchClass", Args);
    assert(R.IsErr());
    assert(R.GetError().find("class_not_found") != MString::npos);
    std::printf("ok: TestClassNotFound\n");
}

/** @brief TestNewThrows - :new 内部 error() 应把错误消息透传到 Err */
static void TestNewThrows() {
    SetupEnv("Bad = {}\n"
             "function Bad:new() error('boom') end\n");

    TScriptArgs Args;
    auto        R = GEngine->CreateInstanceByClassName("Bad", Args);
    assert(R.IsErr());
    assert(R.GetError().find("boom") != MString::npos);
    std::printf("ok: TestNewThrows\n");
}

/** @brief TestMethodCall - 调用 :take_damage(10) 应把 hp 从 100 降到 90 */
static void TestMethodCall() {
    SetupEnv("Monster = {}; Monster.__index = Monster\n"
             "function Monster:new(name, hp) return setmetatable({name=name, hp=hp}, Monster) end\n"
             "function Monster:take_damage(d) self.hp = self.hp - d; return self.hp end\n");

    TScriptArgs Args;
    TVariant    V0[] = {MakeVarStr("goblin"), MakeVarInt(100)};
    Args.Values      = V0;
    Args.Count       = 2;

    auto R = GEngine->CreateInstanceByClassName("Monster", Args);
    assert(R.IsOk());
    TScriptInstanceHandle H = R.GetValue();

    TScriptArgs DamageArgs;
    TVariant    DV[]  = {MakeVarInt(10)};
    DamageArgs.Values = DV;
    DamageArgs.Count  = 1;

    auto M = GEngine->InvokeInstanceMethod(H, "take_damage", DamageArgs);
    assert(M.IsOk());
    auto& V = M.GetValue();
    assert(V.GetType() == EVariantType::Double);
    assert((int64)V.AsDouble().GetValue() == 90);
    std::printf("ok: TestMethodCall\n");
    GEngine->ReleaseInstance(H);
}

/** @brief TestMethodNotFound - 方法不存在应返回含 "method_not_found" 的 Err */
static void TestMethodNotFound() {
    SetupEnv("Foo = {}; Foo.__index = Foo\n"
             "function Foo:new() return setmetatable({}, Foo) end\n");

    TScriptArgs Args;
    auto        R = GEngine->CreateInstanceByClassName("Foo", Args);
    assert(R.IsOk());
    TScriptInstanceHandle H = R.GetValue();

    auto M = GEngine->InvokeInstanceMethod(H, "nonexistent", Args);
    assert(M.IsErr());
    assert(M.GetError().find("method_not_found") != MString::npos);
    std::printf("ok: TestMethodNotFound\n");
    GEngine->ReleaseInstance(H);
}

/**
 * @brief TestReleaseAfterGC - 类表被置 nil 并强制 GC 后,再调方法仍应返回 Err(不得 panic)。
 */
static void TestReleaseAfterGC() {
    SetupEnv("Monster = {}; Monster.__index = Monster\n"
             "function Monster:new(name) return setmetatable({name=name}, Monster) end\n");

    TScriptArgs Args;
    TVariant    V0[] = {MakeVarStr("ephemeral")};
    Args.Values      = V0;
    Args.Count       = 1;

    auto R = GEngine->CreateInstanceByClassName("Monster", Args);
    assert(R.IsOk());
    TScriptInstanceHandle H = R.GetValue();

    // 把 Monster 类表释放 + 强制 GC,触发 instance 被回收
    // Monster 实例的 metatable 仍引用 Monster,但 Lua 5.4 GC 能识别循环
    lua_State* L = GEngine->GetStateForReload().GetLuaState();
    lua_pushnil(L);
    lua_setglobal(L, "Monster");
    // 多次 GC 走完 mark + sweep
    for (int i = 0; i < 5; ++i) {
        lua_gc(L, LUA_GCCOLLECT, 0);
    }

    // 访问方法触发 instance_released
    auto M = GEngine->InvokeInstanceMethod(H, "anything", Args);
    // 如果 instance 没被回收,方法名不存在也走 method_not_found
    // 接受任一错误路径(测试目标:不 panic)
    assert(M.IsErr());
    std::printf("ok: TestReleaseAfterGC (err='%s')\n", M.GetError().c_str());
}

/** @brief TestReleaseExplicit - ReleaseInstance 之后再调方法应返回 Err */
static void TestReleaseExplicit() {
    SetupEnv("Foo = {}; Foo.__index = Foo\n"
             "function Foo:new() return setmetatable({x=42}, Foo) end\n");

    TScriptArgs Args;
    auto        R = GEngine->CreateInstanceByClassName("Foo", Args);
    assert(R.IsOk());
    TScriptInstanceHandle H = R.GetValue();

    GEngine->ReleaseInstance(H);

    auto M = GEngine->InvokeInstanceMethod(H, "anything", Args);
    assert(M.IsErr());
    std::printf("ok: TestReleaseExplicit\n");
}

/**
 * @brief main - 建引擎并依次执行 8 个用例。
 * @return 恒为 0(断言失败会直接 abort)
 */
int main() {
    MLuaEngine Engine;
    Engine.Initialize(SScriptEngineConfig{});
    GEngine = &Engine;

    TestBasicCreate();
    TestFallbackCall();
    TestClassNotFound();
    TestNewThrows();
    TestMethodCall();
    TestMethodNotFound();
    TestReleaseAfterGC();
    TestReleaseExplicit();

    std::printf("ALL OK\n");
    return 0;
}