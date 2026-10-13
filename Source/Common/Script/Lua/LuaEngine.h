/**
 * @file LuaEngine.h
 * @brief MLuaEngine —— Lua 版 IScriptEngine 实现。
 *
 * @detail 持有唯一的活跃 VM(State)、DualVM swap 期间的旧 VM(PendingOldState),
 *   以及按 MClass 索引的模块表与按 ActorId 索引的 proxy actor 表。
 *   StateMutex 保护 VM 切换与访问:调用方在 lua_pcall 期间应持 shared lock
 *   (AcquireReadLock),换 State / 换 VM 时持独占锁(AcquireWriteLock)。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Abstract/EReloadMode.h"
#include "Common/Script/Abstract/EReloadResult.h"
#include "Common/Script/Abstract/EScriptLanguage.h"
#include "Common/Script/Abstract/IScriptEngine.h"
#include "Common/Script/Abstract/IScriptModule.h"
#include "Common/Script/Abstract/SScriptEngineConfig.h"
#include "Common/Script/Abstract/ScriptErrorCodes.h"
#include "Common/Script/Abstract/TVariant.h"
#include "Common/Script/Lua/FPendingCall.h"
#include "Common/Script/Lua/LuaScriptState.h"
#include "Common/Script/Lua/MobDebugServer.h"

#include <shared_mutex>

namespace mession::script::lua {

    // 前向声明:避免在 header 里引入 FLuaPendingCall / MLuaProxyActor 的全定义
    class MLuaEngine;
    struct FLuaPendingCall;
    class MLuaProxyActor;

    /**
     * @brief MLuaEngine - Lua 脚本引擎。
     *
     * @detail 初始化时装配 5 个 stdlib(Mession.RPC / Format / Log / Map / Vector)
     *   与 M.InvokeStatic 桥(MLuaBridge)。热重载有两条路径:Legacy 的 ReplaceState,
     *   以及 DualVM 的 BeginSwap / EndSwap 两阶段协议(见下方说明)。
     *   VM 的 extraspace 存本 engine 指针,供 MobDebug hook 反查。
     *
     * NOTE: actor 持久化相关的 4 个重载目前是 stub —— GetActorHandle 恒返回
     *   kActorNotFound,其余为空实现,因此 DualVM reload 实际不保留 actor state。
     */
    class MLuaEngine : public mession::script::IScriptEngine {
        public:
        MLuaEngine();
        ~MLuaEngine() override;

        TResult<void> Initialize(const mession::script::SScriptEngineConfig& Cfg) override;
        void          Tick(float DeltaSeconds) override;
        void          StepCoroutines() override;
        TResult<void> Shutdown() override;

        /**
         * @brief ReplaceState - Legacy 单 VM 热重载:直接换掉当前 State。
         * @param NewState 新的 VM(所有权移交本 engine)
         * @return 旧 VM 指针(所有权交给调用方,由调用方负责 lua_close)
         *
         * @detail 会 ++VmGeneration 并重绑所有注册模块。保留向后兼容;
         *   新代码应改用 BeginSwap / EndSwap 两阶段协议。
         */
        TUniquePtr<MLuaScriptState> ReplaceState(TUniquePtr<MLuaScriptState> NewState);

        /**
         * @brief BeginSwap - DualVM 两阶段 swap 的 Phase A。
         * @param NewState 新 VM(所有权移交本 engine)
         * @return 旧 VM 指针(所有权交给调用方);无旧 VM 时返回 nullptr
         *
         * @detail 把当前 State 移入 PendingOldState、装入 NewState、++VmGeneration、
         *   给新 VM 写回 extraspace、并重绑所有模块。返回后调用方可安全地在新 VM
         *   上重建 actor / 读旧 VM 的状态,读完必须调 EndSwap 收尾。
         */
        TUniquePtr<MLuaScriptState> BeginSwap(TUniquePtr<MLuaScriptState> NewState);
        /**
         * @brief EndSwap - DualVM 两阶段 swap 的 Phase B,释放 PendingOldState。
         *
         * @detail 若 BeginSwap 的返回值已被调用方持有并释放,PendingOldState 为空,
         *   本函数为 no-op。
         */
        void EndSwap();

        /**
         * @brief AcquireReadLock - 取读路径 RAII 锁,供 lua_pcall 期间持有。
         * @return 生命周期绑定锁对象的 shared lock
         */
        TSharedLock<MSharedMutex> AcquireReadLock();
        /** @brief AcquireWriteLock - 取写路径 RAII 锁(换 State / 换 VM 时使用) */
        TUniqueLock<MSharedMutex> AcquireWriteLock();

        /**
         * @brief DrainPendingCalls - 反复 StepCoroutines 直到无挂起协程或超时。
         * @param TimeoutSeconds 超时上限(秒)
         * @return 成功时 Ok(0);超时返回 Err(kVmDrainTimeout)
         *
         * @detail 每轮 StepCoroutines 后 sleep 10ms;成功时返回值恒为 0(不返回剩余数)。
         */
        TResult<uint32> DrainPendingCalls(uint32 TimeoutSeconds);
        /** @brief CountPendingCalls - 当前挂起的 pending call 数(供 Drain 判断) */
        uint32 CountPendingCalls() const;

        /**
         * @brief InstallStandardLibraries - 把 5 个 stdlib + Bridge 装到指定 VM。
         * @param State 目标 VM
         *
         * @detail 顺序敏感:先 MLuaRpc(负责建 Mession 全局表,其余 Install 幂等复用),
         *   再 Format / Log / Map / Vector,最后 MLuaBridge(push M.InvokeStatic)。
         *   调用方应在 BeginSwap 之前对新 VM 调本函数。
         */
        void InstallStandardLibraries(MLuaScriptState& State);

        /**
         * @brief GetStateForReload - 取当前活跃 VM(不做加锁)。
         *
         * @detail 热重载 / tick 路径的内部 helper;直接解引用 State,调用方须先确认
         *   已 Initialize(否则是 UB)。
         */
        MLuaScriptState& GetStateForReload() {
            return *State;
        }

        /**
         * @brief GetVmGeneration - 当前 VM 世代号。
         * @return 每次 ReplaceState / BeginSwap 会自增的计数,初值 1
         *
         * @detail TScriptInstanceHandle 记录创建时的 generation,用于跨 VM 边界
         *   判定 handle 是否已失效。
         */
        uint32 GetVmGeneration() const {
            return VmGeneration;
        }

        /** @brief GetDebugStatePtr - per-engine 的 MobDebug hook 状态(可能为 nullptr) */
        FMobDebugState* GetDebugStatePtr() {
            return DebugStatePtr;
        }

        /**
         * @brief LoadBufferIntoState - 把 Lua 字节流加载并执行到当前 State(测试/业务辅助)。
         * @param Name   chunk 名(用于报错定位)
         * @param Bytes  源码字节
         * @param Size  字节数
         * @return 空串表示成功;否则为 "engine_not_initialized" 或 VM 错误信息
         */
        MString LoadBufferIntoState(const char* Name, const char* Bytes, size_t Size) {
            if (!State || !State->IsValid())
                return MString("engine_not_initialized");
            return State->LoadBuffer(Name, Bytes, Size);
        }

        // DualVM actor 持久化 — 目前是 no-op stub,见类头的 NOTE
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

        /**
         * @brief Reload 的数据源:外部(file watcher / debug 注入 / 业务 API)
         *   须在调 Reload 之前填入。
         *
         * @detail 为空时 Reload 直接返回 Ok(Success),不做任何切换。
         */
        MString HotReloadNewBytes;

        TResult<EReloadResult> Reload(EReloadMode Mode) override;

        TResult<void> CallFunction(MFunctionObject* Fn, const mession::script::TScriptArgs& Args) override;
        TResult<void> CallFunctionById(uint64 FunctionId, const mession::script::TScriptArgs& Args) override;

        TResult<MObject*> CreateInstance(MClass* Cls, const mession::script::TScriptArgs& Args) override;
        TResult<uint64>   CreateActor(MClass* Cls, const mession::script::TScriptArgs& Args) override;

        TSharedPtr<mession::script::IScriptModule> CreateModule(MClass* OwningClass) override;
        void                                       RegisterModule(TSharedPtr<mession::script::IScriptModule> Module) override;
        void                                       UnregisterModule(MClass* OwningClass) override;

        // C++ 持有脚本侧 class 实例(跨 VM 通用,详见 IScriptEngine.h 契约)
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
        TUniquePtr<MLuaScriptState>                               State;           // 当前活跃 VM
        TUniquePtr<MLuaScriptState>                               PendingOldState; // BeginSwap 后待释放的旧 VM
        TMap<MClass*, TSharedPtr<mession::script::IScriptModule>> Modules;
        TMap<uint64, TSharedPtr<class MLuaProxyActor>>            ActorsById;
        FLuaPendingCallRegistry                                   PendingCalls;
        uint32                                                    VmGeneration = 1; // 初值 1,swap 时自增
        MSharedMutex                                              StateMutex;       // 保护 State / PendingOldState
        FMobDebugState                                            DebugState;       // 本 engine 独占的 hook 状态
        // 裸指针:给 DebugHook 通过 VM extraspace 反查(避免 include 循环)
        FMobDebugState* DebugStatePtr = nullptr;
    };

} // namespace mession::script::lua