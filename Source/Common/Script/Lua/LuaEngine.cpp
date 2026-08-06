#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Script/Lua/LuaTypeBridge.h"
#include "Common/Script/Lua/LuaModule.h"
#include "Common/Script/Lua/LuaCoroutineBridge.h"
#include "Common/Runtime/Log/Log.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace mession::script::lua {

MLuaEngine::MLuaEngine() = default;

MLuaEngine::~MLuaEngine()
{
    if (State)
    {
        Shutdown();
    }
}

TResult<void> MLuaEngine::Initialize(const mession::script::SScriptEngineConfig& /*Cfg*/)
{
    State = std::make_unique<MLuaScriptState>();
    if (!State->IsValid())
    {
        return TResult<void>::Err(MString("luaL_newstate failed"));
    }
    return TResult<void>::Ok();
}

void MLuaEngine::Tick(float /*DeltaSeconds*/) {}
void MLuaEngine::StepCoroutines() {}

TResult<void> MLuaEngine::Shutdown()
{
    Modules.clear();
    State.reset();
    return TResult<void>::Ok();
}

TResult<EReloadResult> MLuaEngine::Reload(EReloadMode /*Mode*/)
{
    return TResult<EReloadResult>::Ok(EReloadResult::Success);
}

TResult<void> MLuaEngine::CallFunction(MFunctionObject* /*Fn*/, const mession::script::TScriptArgs& /*Args*/)
{
    return TResult<void>::Err(MString("CallFunction: full impl pending Task 10/12"));
}

TResult<void> MLuaEngine::CallFunctionById(uint64 /*FunctionId*/, const mession::script::TScriptArgs& /*Args*/)
{
    return TResult<void>::Err(MString("CallFunctionById: full impl pending Task 10/12"));
}

TResult<MObject*> MLuaEngine::CreateInstance(MClass* /*Cls*/, const mession::script::TScriptArgs& /*Args*/)
{
    return TResult<MObject*>::Err(MString("CreateInstance: full impl pending Task 12"));
}

TResult<uint64> MLuaEngine::CreateActor(MClass* /*Cls*/, const mession::script::TScriptArgs& /*Args*/)
{
    return TResult<uint64>::Err(MString("CreateActor: full impl pending Task 12"));
}

TSharedPtr<mession::script::IScriptModule> MLuaEngine::CreateModule(MClass* OwningClass)
{
    if (!State) return nullptr;
    auto Mod = TSharedPtr<mession::script::IScriptModule>(MakeShared<MLuaModule>(State->GetLuaState(), OwningClass));
    Modules[OwningClass] = Mod;
    return Mod;
}

void MLuaEngine::RegisterModule(TSharedPtr<mession::script::IScriptModule> /*Module*/) {}
void MLuaEngine::UnregisterModule(MClass* /*OwningClass*/) {}

void MLuaEngine::SetGlobal(MStringView Key, const mession::script::TVariant& Value)
{
    if (!State || !State->IsValid()) return;
    lua_State* L = State->GetLuaState();

    switch (Value.GetType())
    {
    case mession::script::EVariantType::Int:
        PushInteger(L, Value.AsInt().GetValue());
        break;
    case mession::script::EVariantType::Bool:
        PushBoolean(L, Value.AsBool().GetValue());
        break;
    case mession::script::EVariantType::Double:
        PushNumber(L, Value.AsDouble().GetValue());
        break;
    case mession::script::EVariantType::String:
        PushString(L, Value.AsString().GetValue());
        break;
    case mession::script::EVariantType::Null:
        PushNil(L);
        break;
    }
    MString CKey(Key.data(), Key.size());
    lua_setglobal(L, CKey.c_str());
}

TResult<mession::script::TVariant> MLuaEngine::GetGlobal(MStringView Key)
{
    if (!State || !State->IsValid())
    {
        return TResult<mession::script::TVariant>::Err(MString("engine not initialized"));
    }
    lua_State* L = State->GetLuaState();
    MString CKey(Key.data(), Key.size());
    int Type = lua_getglobal(L, CKey.c_str());
    mession::script::TVariant Out = mession::script::TVariant::MakeNull();
    if (Type == LUA_TNUMBER) Out = mession::script::TVariant::MakeDouble(lua_tonumber(L, -1));
    else if (Type == LUA_TBOOLEAN) Out = mession::script::TVariant::MakeBool(lua_toboolean(L, -1) != 0);
    else if (Type == LUA_TSTRING)
    {
        size_t Len   = 0;
        const char* P = lua_tolstring(L, -1, &Len);
        Out = mession::script::TVariant::MakeString(MString(P, Len));
    }
    lua_pop(L, 1);
    return TResult<mession::script::TVariant>::Ok(Out);
}

MString MLuaEngine::GetErrorString(int32 /*State*/, void* /*Frame*/) const
{
    return MString();
}

} // namespace mession::script::lua