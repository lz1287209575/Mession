#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Abstract/EReloadMode.h"
#include "Common/Script/Abstract/EReloadResult.h"
#include "Common/Script/Abstract/EScriptLanguage.h"
#include "Common/Script/Abstract/IScriptEngine.h"
#include "Common/Script/Abstract/IScriptModule.h"
#include "Common/Script/Abstract/SScriptEngineConfig.h"
#include "Common/Script/Abstract/TVariant.h"
#include "Common/Script/Abstract/ScriptErrorCodes.h"
#include "Common/Script/Lua/LuaScriptState.h"
#include "Common/Script/Lua/FPendingCall.h"

#include <shared_mutex>

namespace mession::script::lua {

    // 前向声明(避免在 header 引入 FLuaPendingCall/MLuaProxyActor 全定义)
    class MLuaEngine;
    struct FLuaPendingCall;
    class MLuaProxyActor;

    class MLuaEngine : public mession::script::IScriptEngine {
        public:
        MLuaEngine();
        ~MLuaEngine() override;

        TResult<void> Initialize(const mession::script::SScriptEngineConfig& Cfg) override;
        void          Tick(float DeltaSeconds) override;
        void          StepCoroutines() override;
        TResult<void> Shutdown() override;

        // Legacy:Hot reload 替换 State(Replace Old State with New State)
        // 旧 State 由调用方持有,负责 lua_close
        // 保留向后兼容;新代码用 BeginSwap / EndSwap 两阶段协议
        TUniquePtr<MLuaScriptState> ReplaceState(TUniquePtr<MLuaScriptState> NewState);

        // DualVM 两阶段 swap:
        //   Phase A: caller 调 BeginSwap(NewState) → installs NewState as live,
        //            bumps VmGeneration, re-binds Modules;returns old state ptr
        //   caller 此时读旧 VM 的 actor state
        //   Phase B: caller 调 EndSwap() → releases PendingOldState (lua_close)
        TUniquePtr<MLuaScriptState> BeginSwap(TUniquePtr<MLuaScriptState> NewState);
        void                        EndSwap();

        // 读路径 RAII 锁(callers 在 lua_pcall 期间持 shared lock)
        TSharedLock<MSharedMutex> AcquireReadLock();
        TUniqueLock<MSharedMutex> AcquireWriteLock();

        // Drain:tick coroutines 直到 pending==0 或 timeout
        TResult<uint32> DrainPendingCalls(uint32 TimeoutSeconds);
        uint32          CountPendingCalls() const;

        // VM_B bootstrap:把 5 个 stdlib 装到新 VM(调用方在 BeginSwap 前先 InstallStandardLibraries)
        void InstallStandardLibraries(MLuaScriptState& State);

        // Hot reload 内部 helper:暴露旧 State 引用供 Drain / CountPendingCalls
        MLuaScriptState& GetStateForReload() {
            return *State;
        }

        // DualVM 计数器:每次 ReplaceState/BeginSwap 时 ++
        // TScriptInstanceHandle.Generation 持它创建时的值;跨 generation 调用立即 fail-fast
        uint32 GetVmGeneration() const {
            return VmGeneration;
        }

        // 测试 / 业务代码加载辅助:把 Lua 字节流加载到当前 State
        MString LoadBufferIntoState(const char* Name, const char* Bytes, size_t Size) {
            if (!State || !State->IsValid())
                return MString("engine_not_initialized");
            return State->LoadBuffer(Name, Bytes, Size);
        }

        // DualVM actor 持久化(T7 stub,T8/T9 真实现)
        TMap<uint64, MString> SaveAllActorStates() override {
            return TMap<uint64, MString>();
        }
        void RestoreAllActorStates(const TMap<uint64, MString>& /*Snapshot*/) override {
        }
        void RebindCrossInstanceRefs() override {
        }
        TResult<TScriptInstanceHandle> GetActorHandle(uint64 /*ActorId*/) override {
            return TResult<TScriptInstanceHandle>::Err(MString(ScriptErrorCodes::kActorNotFound));
        }

        TResult<EReloadResult> Reload(EReloadMode Mode) override;

        TResult<void> CallFunction(MFunctionObject* Fn, const mession::script::TScriptArgs& Args) override;
        TResult<void> CallFunctionById(uint64 FunctionId, const mession::script::TScriptArgs& Args) override;

        TResult<MObject*> CreateInstance(MClass* Cls, const mession::script::TScriptArgs& Args) override;
        TResult<uint64>   CreateActor(MClass* Cls, const mession::script::TScriptArgs& Args) override;

        TSharedPtr<mession::script::IScriptModule> CreateModule(MClass* OwningClass) override;
        void                                       RegisterModule(TSharedPtr<mession::script::IScriptModule> Module) override;
        void                                       UnregisterModule(MClass* OwningClass) override;

        // C++ 持有脚本侧 class 实例(跨 VM 通用)
        TResult<TScriptInstanceHandle> CreateInstanceByClassName(const MString& ClassName, const TScriptArgs& Args) override;
        TResult<TVariant>              InvokeInstanceMethod(TScriptInstanceHandle Handle, const MString& MethodName, const TScriptArgs& Args) override;
        void                           ReleaseInstance(TScriptInstanceHandle Handle) override;

        void                               SetGlobal(MStringView Key, const mession::script::TVariant& Value) override;
        TResult<mession::script::TVariant> GetGlobal(MStringView Key) override;

        MString GetErrorString(int32 State, void* Frame) const override;

        mession::script::EScriptLanguage GetLanguage() const override {
            return mession::script::EScriptLanguage::Lua;
        }
        bool IsSandboxed() const override {
            return false;
        }

        private:
        TUniquePtr<MLuaScriptState>                               State;
        TUniquePtr<MLuaScriptState>                               PendingOldState;
        TMap<MClass*, TSharedPtr<mession::script::IScriptModule>> Modules;
        TMap<uint64, TSharedPtr<class MLuaProxyActor>>            ActorsById;
        FLuaPendingCallRegistry                                   PendingCalls;
        uint32                                                    VmGeneration = 1;
        MSharedMutex                                              StateMutex;
        struct FMobDebugState;                                    // per-engine MobDebug hook 状态(T11)
        FMobDebugState*                                           DebugStatePtr = nullptr;
    };

} // namespace mession::script::lua