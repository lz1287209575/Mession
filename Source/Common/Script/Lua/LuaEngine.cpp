/**
 * @file LuaEngine.cpp
 * @brief 实现 MLuaEngine:VM 生命周期、两阶段热重载、反射/脚本互调与 actor 桥接。
 */
#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Runtime/Id.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Script/Abstract/IScriptEngine.h"
#include "Common/Script/Abstract/ScriptErrorCodes.h"
#include "Common/Script/Abstract/TScriptInstanceHandle.h"
#include "Common/Script/Lua/LuaCoroutineBridge.h"
#include "Common/Script/Lua/LuaHotReload.h"
#include "Common/Script/Lua/LuaModule.h"
#include "Common/Script/Lua/MLuaBridge.h"
#include "Common/Script/Lua/MLuaFormat.h"
#include "Common/Script/Lua/MLuaLog.h"
#include "Common/Script/Lua/MLuaMap.h"
#include "Common/Script/Lua/MLuaProxyActor.h"
#include "Common/Script/Lua/MLuaRpc.h"
#include "Common/Script/Lua/MLuaVector.h"

#include "Common/Script/Lua/LuaTypeBridge.h"
#include <chrono>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    namespace {

        /**
         * @brief PushArgsFromStack - 校验 [FirstArg, LastArg] 区间内全是标量参数。
         * @param L        Lua 栈
         * @param FirstArg 首个实参的栈索引
         * @param LastArg  末个实参的栈索引
         * @return 全是标量时 Ok;出现 table/userdata 等复杂类型返回 Err("unsupported_type_in_args")
         *
         * @detail 目前只做类型白名单校验,并不真正把值写进参数包。
         *   TODO(script): 用 MReflectArchive 反序列化完整 Request。
         */
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

    /**
     * @brief Initialize - 创建 VM 并装配 stdlib。
     * @return VM 创建失败时 Err("luaL_newstate failed"),否则 Ok
     *
     * @detail 配置参数被忽略(见 SScriptEngineConfig.h 的说明)。同时把本 engine
     *   写入 VM extraspace,并把 DebugStatePtr 指向自己的 DebugState。
     *   VmGeneration 保持初值 1(不回滚),因此重复 Initialize 不会重置世代号。
     */
    TResult<void> MLuaEngine::Initialize(const mession::script::SScriptEngineConfig& /*Cfg*/) {
        State = std::make_unique<MLuaScriptState>();
        if (!State->IsValid()) {
            return TResult<void>::Err(MString("luaL_newstate failed"));
        }
        // 绑定 extraspace:让 DebugHook 能从 lua_State 反查到本 engine
        DebugStatePtr = &DebugState;
        State->SetOpaque(this);
        // 装配 5 个 stdlib 到初始 VM(Mession.* 全局表 + M.InvokeStatic)
        InstallStandardLibraries(*State);
        return TResult<void>::Ok();
    }

    /** @brief Tick - 空实现(脚本推进目前依赖 StepCoroutines 单独驱动) */
    void MLuaEngine::Tick(float /*DeltaSeconds*/) {
    }
    /**
     * @brief StepCoroutines - 推进所有待 resume 的协程。
     *
     * @detail 委派给 FLuaPendingCallRegistry::StepAll:只处理
     *   HasResumeArgs() 为真的 call。
     */
    void MLuaEngine::StepCoroutines() {
        PendingCalls.StepAll();
    }

    /**
     * @brief Shutdown - 清空模块表并销毁 VM。
     * @return 恒 Ok
     *
     * @detail 不清理 ActorsById / PendingCalls(见类头的 NOTE)。
     *   析构函数会在 State 仍在时自动调用本函数。
     */
    TResult<void> MLuaEngine::Shutdown() {
        Modules.clear();
        State.reset();
        return TResult<void>::Ok();
    }

    /**
     * @brief ReplaceState - 取写锁,换入 NewState 并返回旧 VM。
     * @return 旧 VM(所有权交给调用方);调用方负责 lua_close
     *
     * @detail ++VmGeneration;对每个模块调 MLuaModule::Rebind 在新 VM 上重建
     *   backing table 并重放已注册的类型/函数/属性。
     */
    TUniquePtr<MLuaScriptState> MLuaEngine::ReplaceState(TUniquePtr<MLuaScriptState> NewState) {
        TUniqueLock                 Lock(StateMutex);
        TUniquePtr<MLuaScriptState> Old = std::move(State);
        State                           = std::move(NewState);
        ++VmGeneration;
        // 重绑所有模块:各自在新 VM 上重建 backing table 并重放反射信息
        for (auto& Kv : Modules) {
            if (auto* LM = dynamic_cast<MLuaModule*>(Kv.second.Get())) {
                LM->Rebind(*State);
            }
        }
        return Old;
    }

    /**
     * @brief BeginSwap - DualVM Phase A:取写锁,把旧 VM 挪进 PendingOldState。
     * @return 旧 VM(所有权交给调用方);无旧 VM 时 nullptr
     *
     * @detail ++VmGeneration;给新 VM 写回 extraspace;重绑所有模块。
     *   旧 VM 上的 MobDebug hook 不会被自动重挂 —— 业务侧需在 swap 后重新 Enable。
     *   返回的指针与 PendingOldState 不会同时持有同一对象(release 后置空)。
     */
    TUniquePtr<MLuaScriptState> MLuaEngine::BeginSwap(TUniquePtr<MLuaScriptState> NewState) {
        TUniqueLock Lock(StateMutex);
        // 旧 VM 留在 PendingOldState,直到 EndSwap 或调用方释放
        PendingOldState = std::move(State);
        State           = std::move(NewState);
        ++VmGeneration;
        // 新 VM extraspace 反查 — 让 DebugHook 仍能找到本 engine
        if (State) {
            State->SetOpaque(this);
        }
        // 旧 VM 上若挂了 MobDebug hook 不再自动重挂(否则 hook 会指向已死的 VM);
        // 业务侧需在 BeginSwap 之后自行 Enable 到新 VM
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

    /**
     * @brief EndSwap - DualVM Phase B:释放 PendingOldState(触发 lua_close)。
     *
     * @detail 若 BeginSwap 的返回值已被调用方持有并释放,则 PendingOldState 为空,
     *   本函数为 no-op。
     */
    void MLuaEngine::EndSwap() {
        TUniqueLock Lock(StateMutex);
        if (PendingOldState) {
            PendingOldState.reset(); // → MLuaScriptState 析构 → lua_close
        }
    }

    /** @brief AcquireReadLock - 读路径 RAII 锁(lua_pcall 期间持有) */
    TSharedLock<MSharedMutex> MLuaEngine::AcquireReadLock() {
        return TSharedLock<MSharedMutex>(StateMutex);
    }
    /** @brief AcquireWriteLock - 写路径 RAII 锁(换 State / 换 VM) */
    TUniqueLock<MSharedMutex> MLuaEngine::AcquireWriteLock() {
        return TUniqueLock<MSharedMutex>(StateMutex);
    }

    /** @brief CountPendingCalls - 委派给 FLuaPendingCallRegistry 计数 */
    uint32 MLuaEngine::CountPendingCalls() const {
        return PendingCalls.CountPending();
    }

    /**
     * @brief DrainPendingCalls - 循环 StepCoroutines 直到 pending 归零或超时。
     * @param TimeoutSeconds 超时上限(秒);0 表示一次调度都不等
     * @return 成功 Ok(0);超时 Err(kVmDrainTimeout)
     *
     * @detail 每轮 StepCoroutines 后 sleep 10ms,超时判定用整秒粒度。
     *   NOTE: 成功路径返回值恒为 0,不反映实际调度次数。
     */
    TResult<uint32> MLuaEngine::DrainPendingCalls(uint32 TimeoutSeconds) {
        // Drain 循环:每轮 StepCoroutines + sleep 10ms,直到 pending==0 或超时
        auto Start = std::chrono::steady_clock::now();
        while (PendingCalls.CountPending() > 0) {
            StepCoroutines();
            auto Elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - Start).count();
            if (Elapsed >= TimeoutSeconds) {
                return TResult<uint32>::Err(MString(ScriptErrorCodes::kVmDrainTimeout));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return TResult<uint32>::Ok(0);
    }

    /**
     * @brief InstallStandardLibraries - 在指定 VM 上装配 4 个 Mession.* 命名空间 + M 桥。
     * @param S 目标 VM;L 为空时直接返回
     *
     * @detail 顺序敏感:先 MLuaRpc(负责建 Mession 全局表),其余 4 个 Install 均为
     *   幂等的 get-or-create。最后装 MLuaBridge,注册 M.InvokeStatic。
     *   NOTE: MLuaRpc::Install 这里传 nullptr 作为 bridge,因此
     *   Mession.RPC.call 在当前装配下必然报 "rpc: bridge not installed"。
     */
    void MLuaEngine::InstallStandardLibraries(MLuaScriptState& S) {
        lua_State* L = S.GetLuaState();
        if (!L)
            return;

        // 先 Rpc(它建 Mession 全局表),其余 4 个按 namespace 字母序、各自幂等
        MLuaRpc::Install(L, /*pEngine*/ nullptr); // 传 nullptr:bridge 未接入,见上方 NOTE
        MLuaFormat::Install(L);
        MLuaLog::Install(L);
        MLuaMap::Install(L);
        MLuaVector::Install(L);

        // Bridge:暴露 M.InvokeStatic 给 MHeaderTool 生成的绑定代码用
        MLuaBridge::Install(L, this);
    }

    /**
     * @brief Reload - 按 Mode 派发到对应热重载实现。
     * @param Mode 重载模式
     * @return DualVM / AtomicSwap 的结果;未知模式返回 Err("unknown_reload_mode")
     *
     * @detail HotReloadNewBytes 为空时直接返回 Ok(Success),不做任何切换。
     *   Discard 当前未单独实现:告警后回退到 AtomicSwap。
     */
    TResult<EReloadResult> MLuaEngine::Reload(EReloadMode Mode) {
        // 数据源由外部(file watcher / debug 注入 / 业务 API)在调用前填入;空则 no-op
        if (HotReloadNewBytes.empty()) {
            return TResult<EReloadResult>::Ok(EReloadResult::Success);
        }
        switch (Mode) {
        case EReloadMode::AtomicSwap:
            return MLuaHotReload::ReloadAtomicSwap(*this, HotReloadNewBytes);
        case EReloadMode::DualVM:
            return MLuaHotReload::ReloadDualVM(*this, HotReloadNewBytes);
        case EReloadMode::Discard:
            // Discard 尚未单独实现:退化为 AtomicSwap
            LOG_WARN("Reload: Discard mode not fully implemented, falling back to AtomicSwap");
            return MLuaHotReload::ReloadAtomicSwap(*this, HotReloadNewBytes);
        }
        return TResult<EReloadResult>::Err(MString("unknown_reload_mode"));
    }

    /**
     * @brief CallFunction - 调用一个反射函数。
     * @param Fn   反射函数对象;为 nullptr 时 Err("function_null")
     * @param Args 实参包
     * @return 成功 Ok;失败 Err(错误文本)
     *
     * @detail 两条路径,按优先级:
     *   1. Fn->NativeInvoke 非空 —— 走真正的 C++ 反射 invoke(需要 MHeaderTool emit
     *      <Class>_<Method>_NativeInvoke);此路径忽略 Args,只做空参数调用。
     *   2. 否则 fallback 到 Lua 侧:lua_getglobal(ClassName).<MethodName>(self, args),
     *      适用于"业务类完全写在 Lua 里"的情况。
     *   出错或返回时都会把 Lua 栈恢复到入口高度(OldTop),不留残留。
     */
    TResult<void> MLuaEngine::CallFunction(MFunctionObject* Fn, const mession::script::TScriptArgs& Args) {
        if (!Fn) {
            return TResult<void>::Err(MString("function_null"));
        }
        if (!State || !State->IsValid()) {
            return TResult<void>::Err(MString("engine_not_initialized"));
        }

        lua_State* L = State->GetLuaState();

        // 反射桥接路径优先;NativeInvoke 未 emit 时退化为下面的 Lua 全局查找路径

        if (Fn->NativeInvoke != nullptr) {
            // 反射路径(NativeInvoke 已 emit):走 MReflectArchive 读写。
            // NOTE: 这里 In/Out 都是空 archive,方法收到的是全 0 参数。
            int             OldTop = lua_gettop(L);
            MReflectArchive In, Out;
            bool            Ok = Fn->NativeInvoke(nullptr, &In, &Out);
            if (!Ok) {
                lua_settop(L, OldTop);
                return TResult<void>::Err(MString("native_invocation_failed"));
            }
            lua_settop(L, OldTop);
            return TResult<void>::Ok();
        }

        // Fallback:lua_getglobal(ClassName).<MethodName>(self, args)
        std::string ClsName    = Fn->OwnerClass ? Fn->OwnerClass->GetName() : "";
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

        // 把 class 表插到方法之前,作为 self 实参
        lua_insert(L, -2);

        // 依次压入实参
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

        // self + N 个实参,共 N+1 个调用参数;不要返回值
        int N = lua_pcall(L, static_cast<int>(Args.Count) + 1, 0, 0);
        if (N != LUA_OK) {
            MString Err = lua_isstring(L, -1) ? lua_tostring(L, -1) : MString("pcall_failed");
            lua_settop(L, OldTop);
            return TResult<void>::Err(Err);
        }

        lua_settop(L, OldTop);
        return TResult<void>::Ok();
    }

    /**
     * @brief CallFunctionById - 按函数 id 在硬编码的两个类里查找后调用。
     * @param FunctionId 反射函数 id(截断为 uint16 后查找)
     * @param Args       实参包
     * @return 找到即转发 CallFunction 的结果;未找到 Err("function_id_not_found")
     *
     * @detail NOTE: 目前只在 {"MEchoService", "MServiceRegistry"} 两个类里搜,
     *   属临时实现,需要换成全局 FunctionId 索引。
     */
    TResult<void> MLuaEngine::CallFunctionById(uint64 FunctionId, const mession::script::TScriptArgs& Args) {
        if (!State || !State->IsValid()) {
            return TResult<void>::Err(MString("engine_not_initialized"));
        }

        // FunctionId → MFunction 穷举查询(临时:只覆盖这两个类)
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

    /**
     * @brief CreateInstance - 反射创建 MObject(尚未实现)。
     * @return Cls 为空时 Err("class_null");否则恒 Err("create_instance_pending_full_impl")
     *
     * @detail 契约要求返回裸 MObject*,而现网创建路径产出 TSharedPtr<MObject>,
     *   所有权语义未定,故先占位。
     *   TODO(script): 换成 MObject::NewInstance<T>(Args) 并明确所有权转移规则。
     */
    TResult<MObject*> MLuaEngine::CreateInstance(MClass* Cls, const mession::script::TScriptArgs& Args) {
        if (!Cls) {
            return TResult<MObject*>::Err(MString("class_null"));
        }
        static MObject* Stub = nullptr;
        Stub                 = Stub; // 防 unused warning
        (void)Args;
        return TResult<MObject*>::Err(MString("create_instance_pending_full_impl"));
    }

    /**
     * @brief CreateActor - 在脚本侧建实例并包成 MLuaProxyActor 登记。
     * @param Cls  目标类(取 GetName() 作为脚本侧类名)
     * @param Args 构造实参
     * @return 新分配的 ActorId(MUniqueIdGenerator 生成)
     *
     * @detail 三步:Lua 侧 CreateInstanceByClassName → 生成 ActorId 并存入 ActorsById
     *   (TSharedPtr 共享所有权)→ 触发 OnCreated 钩子。
     */
    TResult<uint64> MLuaEngine::CreateActor(MClass* Cls, const mession::script::TScriptArgs& Args) {
        if (!Cls) {
            return TResult<uint64>::Err(MString("class_null"));
        }
        if (!State || !State->IsValid()) {
            return TResult<uint64>::Err(MString("engine_not_initialized"));
        }

        // 1. Lua 端按 ClassName 创建实例
        MString ClassName      = Cls->GetName();
        auto    InstanceResult = CreateInstanceByClassName(ClassName, Args);
        if (InstanceResult.IsErr()) {
            return TResult<uint64>::Err(InstanceResult.GetError());
        }
        TScriptInstanceHandle Handle = InstanceResult.GetValue();

        // 2. 生成 ActorId 并构造 MLuaProxyActor(此后由 ActorsById 共享持有)
        uint64 ActorId      = MUniqueIdGenerator::Generate();
        auto   Proxy        = MakeShared<MLuaProxyActor>(*this, Cls, ActorId, Handle, ClassName);
        ActorsById[ActorId] = Proxy;

        // 3. 触发 OnCreated 钩子
        Proxy->OnCreated();

        return TResult<uint64>::Ok(ActorId);
    }

    /**
     * @brief CreateModule - 为 OwningClass 建 MLuaModule 并登记到 Modules。
     * @return 新建模块;State 未初始化时返回 nullptr
     *
     * @detail 同一 OwningClass 重复调用会覆盖旧模块(旧模块随之析构、unref 自己的表)。
     */
    TSharedPtr<mession::script::IScriptModule> MLuaEngine::CreateModule(MClass* OwningClass) {
        if (!State)
            return nullptr;
        auto Mod             = TSharedPtr<mession::script::IScriptModule>(MakeShared<MLuaModule>(State->GetLuaState(), OwningClass));
        Modules[OwningClass] = Mod;
        return Mod;
    }

    /** @brief RegisterModule - 空实现:模块在 CreateModule 时已登记 */
    void MLuaEngine::RegisterModule(TSharedPtr<mession::script::IScriptModule> /*Module*/) {
    }
    /** @brief UnregisterModule - 空实现:模块由 Modules 表生命周期管理 */
    void MLuaEngine::UnregisterModule(MClass* /*OwningClass*/) {
    }

    /**
     * @brief SetGlobal - 把 TVariant 值写入 Lua 全局变量。
     * @param Key   全局变量名(内部按 data/size 构造临时 MString,兼容非 NUL 结尾)
     * @param Value 值;Null 会写入 nil(即删除该全局)
     *
     * @detail VM 未初始化时静默返回。
     */
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

    /**
     * @brief GetGlobal - 读 Lua 全局变量。
     * @param Key 全局变量名
     * @return 成功 Ok(TVariant);number → Double,boolean → Bool,string → String,
     *   其余类型(含 nil/table/function)一律 → Null
     *
     * @detail 注意 Lua number 统一映射为 Double,整数会丢精度区分。
     */
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

    /**
     * @brief CreateInstanceByClassName - 在 Lua 全局表里按类名建实例。
     * @param ClassName 全局类名(必须是一个 table)
     * @param Args      构造实参
     * @return 句柄(registry ref + 当前 VmGeneration);失败返回 Err
     *
     * @detail 两条分支:类有 `new` 字段则调 `ClassName.new(self, args)`(期望返回 1 个值);
     *   否则退化为直接调 table 本身(靠 __call 元方法)。
     *   返回 nil → Err(kFactoryReturnNil)。成功时用 luaL_ref 把实例锚在 registry,
     *   句柄因此在 Lua GC 回收该实例前一直有效。
     */
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

    /**
     * @brief InvokeInstanceMethod - 在句柄对应的 Lua 实例上调用方法。
     * @param Handle     实例句柄
     * @param MethodName 方法名
     * @param Args       实参(标量)
     * @return Ok(TVariant):nil→Null / boolean→Bool / number→Double / string→String,
     *   其余返回类型(table/function 等)退化为 Null
     *
     * @detail 失败路径 Err(kInvalidArg / kInstanceReleased / kMethodNotFound / VM 错误文本)。
     *   NOTE: 本函数只校验 Handle.IsValid(),不做 generation 比对 —— 跨 VM 的陈旧
     *   句柄通常因 registry ref 失效而报 kInstanceReleased,而非显式的 kVmSwapped。
     */
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

        // 目标必须是 table / userdata(才能走 __index);标量与 nil 一律拒绝
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

    /**
     * @brief ReleaseInstance - 释放句柄在 registry 中的引用(幂等)。
     * @param Handle 目标句柄;无效句柄或 VM 未初始化时静默返回
     *
     * @detail 不比对 generation:对旧 VM 的句柄调本函数会作用在当前 VM 的 registry 上,
     *   可能误释放同号的 ref(见 InvokeInstanceMethod 的 NOTE)。
     */
    void MLuaEngine::ReleaseInstance(TScriptInstanceHandle Handle) {
        if (!Handle.IsValid())
            return;
        if (!State || !State->IsValid())
            return;
        luaL_unref(State->GetLuaState(), LUA_REGISTRYINDEX, Handle.GetId());
    }

    /**
     * @brief GetErrorString - 未实现。
     * @return 恒返回空串
     *
     * @detail 当前调用方拿到空串时应自行回退到 VM 返回的错误文本。
     */
    MString MLuaEngine::GetErrorString(int32 /*State*/, void* /*Frame*/) const {
        return MString();
    }

} // namespace mession::script::lua