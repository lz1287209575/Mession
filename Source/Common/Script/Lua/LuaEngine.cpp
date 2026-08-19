#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Runtime/Id.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Script/Abstract/IScriptEngine.h"
#include "Common/Script/Abstract/ScriptErrorCodes.h"
#include "Common/Script/Abstract/TScriptInstanceHandle.h"
#include "Common/Script/Lua/LuaCoroutineBridge.h"
#include "Common/Script/Lua/LuaModule.h"

#include <chrono>
#include "Common/Script/Lua/LuaTypeBridge.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    namespace {

        // 把 Lua 栈参数转 TResult(MString) 错误流,标量支持(int64 / double / bool / MString)
        // 复杂类型抛 "unsupported_type_in_args" TODO:用 MReflectArchive 反序列化完整 Request
        TResult<void> PushArgsFromStack(lua_State* L, MFunctionObject* /*Fn*/, int FirstArg, int LastArg) {
            for (int i = FirstArg; i <= LastArg; ++i) {
                int Type = lua_type(L, i);
                if (Type != LUA_TNIL && Type != LUA_TNUMBER && Type != LUA_TBOOLEAN && Type != LUA_TSTRING) {
                    return TResult<void>::Err(MString("unsupported_type_in_args"));
                }
                (void)Type;
            }
            return TResult<void>::Ok();
        }

    } // namespace

    MLuaEngine::MLuaEngine() = default;

    MLuaEngine::~MLuaEngine() {
        if (State) {
            Shutdown();
        }
    }

    TResult<void> MLuaEngine::Initialize(const mession::script::SScriptEngineConfig& /*Cfg*/) {
        State = std::make_unique<MLuaScriptState>();
        if (!State->IsValid()) {
            return TResult<void>::Err(MString("luaL_newstate failed"));
        }
        return TResult<void>::Ok();
    }

    void MLuaEngine::Tick(float /*DeltaSeconds*/) {
    }
    void MLuaEngine::StepCoroutines() {
        // 走 FLuaPendingCallRegistry:把所有 bResumePending 的 call 真正 resume
        PendingCalls.StepAll();
    }

    TResult<void> MLuaEngine::Shutdown() {
        Modules.clear();
        State.reset();
        return TResult<void>::Ok();
    }

    TUniquePtr<MLuaScriptState> MLuaEngine::ReplaceState(TUniquePtr<MLuaScriptState> NewState) {
        TUniqueLock Lock(StateMutex);
        TUniquePtr<MLuaScriptState> Old = std::move(State);
        State                           = std::move(NewState);
        ++VmGeneration;
        // 触发 modules 重 bind (Task 3 后续 hook)
        for (auto& Kv : Modules) {
            if (auto* LM = dynamic_cast<MLuaModule*>(Kv.second.Get())) {
                LM->Rebind(*State);
            }
        }
        return Old;
    }

    TUniquePtr<MLuaScriptState> MLuaEngine::BeginSwap(TUniquePtr<MLuaScriptState> NewState) {
        TUniqueLock Lock(StateMutex);
        // 旧 VM 保留在 PendingOldState 直到 EndSwap 由 caller 主动释放
        PendingOldState = std::move(State);
        State           = std::move(NewState);
        ++VmGeneration;
        // Rebind 所有 modules — 每个 module 重新 luaL_ref + 重放 reflection
        for (auto& Kv : Modules) {
            if (auto* LM = dynamic_cast<MLuaModule*>(Kv.second.Get())) {
                LM->Rebind(*State);
            }
        }
        // 返回旧 VM ptr(caller 用于读 actor state);caller 读完调 EndSwap 释放
        if (PendingOldState) {
            // 转移所有权给 caller(避免 BeginSwap 返回值与 PendingOldState 同时持同一 ptr)
            return TUniquePtr<MLuaScriptState>(PendingOldState.release());
        }
        return nullptr;
    }

    void MLuaEngine::EndSwap() {
        TUniqueLock Lock(StateMutex);
        // PendingOldState 此时可能已被 caller 释放(从 BeginSwap 拿走的 ptr)
        // 如果还在,这里释放并触发 lua_close
        if (PendingOldState) {
            PendingOldState.reset();  // → MLuaScriptState 析构 → lua_close
        }
    }

    TSharedLock<MSharedMutex> MLuaEngine::AcquireReadLock() {
        return TSharedLock<MSharedMutex>(StateMutex);
    }
    TUniqueLock<MSharedMutex> MLuaEngine::AcquireWriteLock() {
        return TUniqueLock<MSharedMutex>(StateMutex);
    }

    uint32 MLuaEngine::CountPendingCalls() const {
        return PendingCalls.CountPending();
    }

    TResult<uint32> MLuaEngine::DrainPendingCalls(uint32 TimeoutSeconds) {
        // Drain loop:StepCoroutines 每 10ms 一次,直到 pending==0 或 timeout
        auto Start = std::chrono::steady_clock::now();
        while (PendingCalls.CountPending() > 0) {
            StepCoroutines();
            auto Elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - Start).count();
            if (Elapsed >= TimeoutSeconds) {
                return TResult<uint32>::Err(MString(ScriptErrorCodes::kVmDrainTimeout));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return TResult<uint32>::Ok(0);
    }

    void MLuaEngine::InstallStandardLibraries(MLuaScriptState& /*State*/) {
        // Stub:T12 完成后实做;按序调 5 个 stdlib Install
        // 每个 Install 都是 idempotent get-or-create,所以多次调用安全
    }

    TResult<EReloadResult> MLuaEngine::Reload(EReloadMode /*Mode*/) {
        return TResult<EReloadResult>::Ok(EReloadResult::Success);
    }

    TResult<void> MLuaEngine::CallFunction(MFunctionObject* Fn, const mession::script::TScriptArgs& Args) {
        if (!Fn) {
            return TResult<void>::Err(MString("function_null"));
        }
        if (!State || !State->IsValid()) {
            return TResult<void>::Err(MString("engine_not_initialized"));
        }

        lua_State* L = State->GetLuaState();

        // 反射桥接路径(优先):走 NativeInvoke
        // 真实反射(请求/响应序列化)需要 MHeaderTool emit <Class>_<Method>_NativeInvoke
        // 当前 MFunctionObject.NativeInvoke 字段未填,所以走 fallback Lua 路径:
        // lua_getglobal(ClassName).<MethodName>(args)
        // 完整 MHeaderTool 反射放后续 plan

        if (Fn->NativeInvoke != nullptr) {
            // 反射路径(NativeInvoke 已 emit 时)
            int               OldTop = lua_gettop(L);
            MReflectArchive   In, Out;
            bool              Ok = Fn->NativeInvoke(nullptr, &In, &Out);
            if (!Ok) {
                lua_settop(L, OldTop);
                return TResult<void>::Err(MString("native_invocation_failed"));
            }
            lua_settop(L, OldTop);
            return TResult<void>::Ok();
        }

        // Fallback:lua_getglobal(classname).<method>(args)
        // 适用"业务类完全在 Lua 侧"的情况(MEchoService 已实现)
        std::string ClsName = Fn->OwnerClass ? Fn->OwnerClass->GetName() : "";
        std::string MethodName = Fn->Name;
        if (ClsName.empty() || MethodName.empty()) {
            return TResult<void>::Err(MString("no_owner_or_method_name"));
        }

        int OldTop = lua_gettop(L);

        // 拿 class
        lua_getglobal(L, ClsName.c_str());
        if (!lua_istable(L, -1)) {
            lua_settop(L, OldTop);
            return TResult<void>::Err(MString("class_not_found: ") + ClsName);
        }

        // 拿 method
        lua_getfield(L, -1, MethodName.c_str());
        if (!lua_isfunction(L, -1)) {
            lua_settop(L, OldTop);
            return TResult<void>::Err(MString("method_not_found: ") + MethodName);
        }

        // 插入 self(class)到 args 前
        lua_insert(L, -2);

        // Push args
        for (size_t i = 0; i < Args.Count; ++i) {
            switch (Args.Values[i].GetType()) {
            case EVariantType::Int:    PushInteger(L, Args.Values[i].AsInt().GetValue()); break;
            case EVariantType::Bool:   PushBoolean(L, Args.Values[i].AsBool().GetValue()); break;
            case EVariantType::Double: PushNumber(L, Args.Values[i].AsDouble().GetValue()); break;
            case EVariantType::String: PushString(L, Args.Values[i].AsString().GetValue()); break;
            case EVariantType::Null:   PushNil(L); break;
            }
        }

        // self + args=N+1 args
        int N = lua_pcall(L, static_cast<int>(Args.Count) + 1, 0, 0);
        // 不推栈,业务类已实现 ipairs/event 模式
        if (N != LUA_OK) {
            MString Err = lua_isstring(L, -1) ? lua_tostring(L, -1) : MString("pcall_failed");
            lua_settop(L, OldTop);
            return TResult<void>::Err(Err);
        }

        lua_settop(L, OldTop);
        return TResult<void>::Ok();
    }

    TResult<void> MLuaEngine::CallFunctionById(uint64 FunctionId, const mession::script::TScriptArgs& Args) {
        if (!State || !State->IsValid()) {
            return TResult<void>::Err(MString("engine_not_initialized"));
        }

        // FunctionId → MFunction 全局查询
        for (auto& ClsName : {"MEchoService", "MServiceRegistry"}) {
            MClass* Cls = MObject::FindClass(ClsName);
            if (!Cls)
                continue;
            MFunctionObject* Fn = Cls->FindFunctionById(static_cast<uint16>(FunctionId));
            if (Fn)
                return CallFunction(Fn, Args);
        }
        return TResult<void>::Err(MString("function_id_not_found"));
    }

    TResult<MObject*> MLuaEngine::CreateInstance(MClass* Cls, const mession::script::TScriptArgs& Args) {
        if (!Cls) {
            return TResult<MObject*>::Err(MString("class_null"));
        }
        // 简化:CreateInstance 实际返回 TSharedPtr<MObject>,但本接口要求 raw MObject*
        // TODO:用 MObject::NewInstance<T>(Args) 替换,目前 spec 还没要求具体类型
        static MObject* Stub = nullptr;
        Stub                 = Stub; // 防 unused warning
        (void)Args;
        return TResult<MObject*>::Err(MString("create_instance_pending_full_impl"));
    }

    TResult<uint64> MLuaEngine::CreateActor(MClass* Cls, const mession::script::TScriptArgs& Args) {
        if (!Cls) {
            return TResult<uint64>::Err(MString("class_null"));
        }
        // 简化:生成 ActorId + 注册到 MActorRouter
        // Object 本体留给业务侧业务逻辑注册(走 OnNewActor 回调)
        uint64 ActorId = MUniqueIdGenerator::Generate();
        MActorRouter::Get().RegisterActor(ActorId, EServerType::Unknown);
        (void)Args;
        return TResult<uint64>::Ok(ActorId);
    }

    TSharedPtr<mession::script::IScriptModule> MLuaEngine::CreateModule(MClass* OwningClass) {
        if (!State)
            return nullptr;
        auto Mod             = TSharedPtr<mession::script::IScriptModule>(MakeShared<MLuaModule>(State->GetLuaState(), OwningClass));
        Modules[OwningClass] = Mod;
        return Mod;
    }

    void MLuaEngine::RegisterModule(TSharedPtr<mession::script::IScriptModule> /*Module*/) {
    }
    void MLuaEngine::UnregisterModule(MClass* /*OwningClass*/) {
    }

    void MLuaEngine::SetGlobal(MStringView Key, const mession::script::TVariant& Value) {
        if (!State || !State->IsValid())
            return;
        lua_State* L = State->GetLuaState();

        switch (Value.GetType()) {
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

    TResult<mession::script::TVariant> MLuaEngine::GetGlobal(MStringView Key) {
        if (!State || !State->IsValid()) {
            return TResult<mession::script::TVariant>::Err(MString("engine_not_initialized"));
        }
        lua_State*                L = State->GetLuaState();
        MString                   CKey(Key.data(), Key.size());
        int                       Type = lua_getglobal(L, CKey.c_str());
        mession::script::TVariant Out  = mession::script::TVariant::MakeNull();
        if (Type == LUA_TNUMBER)
            Out = mession::script::TVariant::MakeDouble(lua_tonumber(L, -1));
        else if (Type == LUA_TBOOLEAN)
            Out = mession::script::TVariant::MakeBool(lua_toboolean(L, -1) != 0);
        else if (Type == LUA_TSTRING) {
            size_t      Len = 0;
            const char* P   = lua_tolstring(L, -1, &Len);
            Out             = mession::script::TVariant::MakeString(MString(P, Len));
        }
        lua_pop(L, 1);
        return TResult<mession::script::TVariant>::Ok(Out);
    }

    TResult<TScriptInstanceHandle> MLuaEngine::CreateInstanceByClassName(const MString& ClassName, const TScriptArgs& Args) {
        if (!State || !State->IsValid()) {
            return TResult<TScriptInstanceHandle>::Err(MString("engine_not_initialized"));
        }
        lua_State* L      = State->GetLuaState();
        int        OldTop = lua_gettop(L);

        lua_getglobal(L, ClassName.c_str());
        if (!lua_istable(L, -1)) {
            lua_settop(L, OldTop);
            return TResult<TScriptInstanceHandle>::Err(MString(ScriptErrorCodes::kClassNotFound) + MString(": ") + ClassName);
        }

        bool bUseNew = false;
        lua_getfield(L, -1, "new");
        if (lua_isfunction(L, -1)) {
            bUseNew = true;
        }
        lua_pop(L, 1);

        if (bUseNew) {
            lua_getfield(L, -1, "new");
            lua_insert(L, -2);
            for (size_t i = 0; i < Args.Count; ++i) {
                switch (Args.Values[i].GetType()) {
                case EVariantType::Int:
                    PushInteger(L, Args.Values[i].AsInt().GetValue());
                    break;
                case EVariantType::Bool:
                    PushBoolean(L, Args.Values[i].AsBool().GetValue());
                    break;
                case EVariantType::Double:
                    PushNumber(L, Args.Values[i].AsDouble().GetValue());
                    break;
                case EVariantType::String:
                    PushString(L, Args.Values[i].AsString().GetValue());
                    break;
                case EVariantType::Null:
                    PushNil(L);
                    break;
                }
            }
            if (lua_pcall(L, static_cast<int>(Args.Count) + 1, 1, 0) != LUA_OK) {
                MString Err = lua_isstring(L, -1) ? lua_tostring(L, -1) : MString("lua_call_failed");
                lua_settop(L, OldTop);
                return TResult<TScriptInstanceHandle>::Err(MString("[new_path] ") + Err);
            }
        } else {
            for (size_t i = 0; i < Args.Count; ++i) {
                switch (Args.Values[i].GetType()) {
                case EVariantType::Int:
                    PushInteger(L, Args.Values[i].AsInt().GetValue());
                    break;
                case EVariantType::Bool:
                    PushBoolean(L, Args.Values[i].AsBool().GetValue());
                    break;
                case EVariantType::Double:
                    PushNumber(L, Args.Values[i].AsDouble().GetValue());
                    break;
                case EVariantType::String:
                    PushString(L, Args.Values[i].AsString().GetValue());
                    break;
                case EVariantType::Null:
                    PushNil(L);
                    break;
                }
            }
            if (lua_pcall(L, static_cast<int>(Args.Count), 1, 0) != LUA_OK) {
                MString Err = lua_isstring(L, -1) ? lua_tostring(L, -1) : MString("lua_call_failed");
                lua_settop(L, OldTop);
                return TResult<TScriptInstanceHandle>::Err(Err);
            }
        }

        if (lua_isnil(L, -1)) {
            lua_settop(L, OldTop);
            return TResult<TScriptInstanceHandle>::Err(MString(ScriptErrorCodes::kFactoryReturnNil));
        }

        int Ref = luaL_ref(L, LUA_REGISTRYINDEX);
        return TResult<TScriptInstanceHandle>::Ok(TScriptInstanceHandle(Ref, VmGeneration));
    }

    TResult<TVariant> MLuaEngine::InvokeInstanceMethod(TScriptInstanceHandle Handle, const MString& MethodName, const TScriptArgs& Args) {
        if (!Handle.IsValid()) {
            return TResult<TVariant>::Err(MString(ScriptErrorCodes::kInvalidArg));
        }
        if (!State || !State->IsValid()) {
            return TResult<TVariant>::Err(MString("engine_not_initialized"));
        }
        lua_State* L      = State->GetLuaState();
        int        OldTop = lua_gettop(L);

        lua_rawgeti(L, LUA_REGISTRYINDEX, Handle.GetId());
        if (lua_isnil(L, -1)) {
            lua_settop(L, OldTop);
            return TResult<TVariant>::Err(MString(ScriptErrorCodes::kInstanceReleased));
        }

        // 对象必须是 table / userdata(支持 __index);number / string / bool / nil 都不行
        int ObjType = lua_type(L, -1);
        if (ObjType != LUA_TTABLE && ObjType != LUA_TUSERDATA) {
            lua_settop(L, OldTop);
            return TResult<TVariant>::Err(MString("[invalid_arg] instance is not a table/userdata (type=") + MString(std::to_string(ObjType).c_str()) + MString(")"));
        }

        lua_getfield(L, -1, MethodName.c_str());
        if (!lua_isfunction(L, -1)) {
            lua_settop(L, OldTop);
            return TResult<TVariant>::Err(MString(ScriptErrorCodes::kMethodNotFound) + MString(": ") + MethodName);
        }

        lua_insert(L, -2);

        for (size_t i = 0; i < Args.Count; ++i) {
            switch (Args.Values[i].GetType()) {
            case EVariantType::Int:
                PushInteger(L, Args.Values[i].AsInt().GetValue());
                break;
            case EVariantType::Bool:
                PushBoolean(L, Args.Values[i].AsBool().GetValue());
                break;
            case EVariantType::Double:
                PushNumber(L, Args.Values[i].AsDouble().GetValue());
                break;
            case EVariantType::String:
                PushString(L, Args.Values[i].AsString().GetValue());
                break;
            case EVariantType::Null:
                PushNil(L);
                break;
            }
        }

        if (lua_pcall(L, static_cast<int>(Args.Count) + 1, 1, 0) != LUA_OK) {
            MString Err = lua_isstring(L, -1) ? lua_tostring(L, -1) : MString("method_call_failed");
            lua_settop(L, OldTop);
            return TResult<TVariant>::Err(Err);
        }

        TResult<TVariant> Out  = TResult<TVariant>::Ok(TVariant::MakeNull());
        int               Type = lua_type(L, -1);
        switch (Type) {
        case LUA_TNIL:
            Out = TResult<TVariant>::Ok(TVariant::MakeNull());
            break;
        case LUA_TBOOLEAN:
            Out = TResult<TVariant>::Ok(TVariant::MakeBool(lua_toboolean(L, -1) != 0));
            break;
        case LUA_TNUMBER:
            Out = TResult<TVariant>::Ok(TVariant::MakeDouble(lua_tonumber(L, -1)));
            break;
        case LUA_TSTRING: {
            size_t      Len = 0;
            const char* P   = lua_tolstring(L, -1, &Len);
            Out             = TResult<TVariant>::Ok(TVariant::MakeString(MString(P, Len)));
            break;
        }
        default:
            Out = TResult<TVariant>::Ok(TVariant::MakeNull());
            break;
        }
        lua_settop(L, OldTop);
        return Out;
    }

    void MLuaEngine::ReleaseInstance(TScriptInstanceHandle Handle) {
        if (!Handle.IsValid())
            return;
        if (!State || !State->IsValid())
            return;
        luaL_unref(State->GetLuaState(), LUA_REGISTRYINDEX, Handle.GetId());
    }

    MString MLuaEngine::GetErrorString(int32 /*State*/, void* /*Frame*/) const {
        return MString();
    }

} // namespace mession::script::lua