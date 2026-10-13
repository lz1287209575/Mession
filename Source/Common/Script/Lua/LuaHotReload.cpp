/**
 * @file LuaHotReload.cpp
 * @brief 实现 DualVM(保 actor 状态)与 AtomicSwap(丢状态)两种 Lua 热重载流程。
 */
#include "Common/Script/Lua/LuaHotReload.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Script/Abstract/ScriptErrorCodes.h"
#include "Common/Script/Lua/FPendingCall.h"
#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Script/Lua/LuaScriptState.h"

#include <chrono>

namespace mession::script::lua {

    namespace {
        // Drain 阶段超时上限(秒) — 暂为常量,后续可提成配置项
        constexpr uint32 kDrainTimeoutSec = 30;
    } // namespace

    /**
     * @brief ReloadDualVM - DualVM 两阶段热重载的完整 10 步流程。
     * @param Engine   目标引擎(须已 Initialize)
     * @param NewBytes 新脚本源码
     * @return Ok(Success);drain 超时则回退并返回 AtomicSwap 的结果;
     *   新建 VM 或 load 失败返回 Err,此时当前 VM 与 generation 均未改变
     *
     * @detail 步骤:
     *   1. drain in-flight 协程(StepCoroutines 直到 PendingCalls==0 或 30s 超时)
     *   2. drain 失败 → 回退 AtomicSwap
     *   3. 快照 actor state(SaveAllActorStates)
     *   4. 新建 VM_B 并装 stdlib
     *   5. LoadBuffer 到 VM_B
     *   6. 失败 → Err(load_failed);VM_A 仍 live,VmGeneration 不变
     *   7. BeginSwap(VM_B) —— 自增 VmGeneration,旧 VM 移入 PendingOldState
     *   8. RestoreAllActorStates(Snapshot)
     *   9. RebindCrossInstanceRefs —— 回调业务的 OnVmSwapped(OldH, NewH)
     *  10. EndSwap —— 释放旧 VM(触发 lua_close)
     */
    TResult<EReloadResult> MLuaHotReload::ReloadDualVM(MLuaEngine& Engine, const MString& NewBytes) {
        // 1. Drain
        auto Drain = Engine.DrainPendingCalls(kDrainTimeoutSec);
        if (Drain.IsErr()) {
            LOG_WARN("ReloadDualVM: drain timeout ({}s), falling back to AtomicSwap", kDrainTimeoutSec);
            return ReloadAtomicSwap(Engine, NewBytes);
        }

        // 3. 快照 actor state(仍读旧 VM,在 BeginSwap 之前)
        auto Snapshot = Engine.SaveAllActorStates();

        // 4. 建立 VM_B
        auto NewState = std::make_unique<MLuaScriptState>();
        if (!NewState->IsValid()) {
            return TResult<EReloadResult>::Err(MString("ReloadDualVM: new VM invalid"));
        }

        // 5. 在 VM_B 上装配 stdlib
        Engine.InstallStandardLibraries(*NewState);

        // 6. LoadBuffer
        MString LoadErr = NewState->LoadBuffer("reload", NewBytes.c_str(), NewBytes.size());
        if (!LoadErr.empty()) {
            return TResult<EReloadResult>::Err(MString("ReloadDualVM: load failed: ") + LoadErr);
        }

        // 7. BeginSwap:VM_A → PendingOldState,VM_B → State,自增 VmGeneration
        TUniquePtr<MLuaScriptState> OldState = Engine.BeginSwap(std::move(NewState));

        // 8. 在新 VM 上恢复 actor
        Engine.RestoreAllActorStates(Snapshot);

        // 9. 重绑跨实例引用(回调 OnVmSwapped)
        Engine.RebindCrossInstanceRefs();

        // 10. EndSwap —— 释放旧 VM
        Engine.EndSwap();
        // OldState 离开作用域再析构一次(此前 BeginSwap 已把所有权交给本变量)

        LOG_INFO("ReloadDualVM: complete, VmGeneration={}", Engine.GetVmGeneration());
        return TResult<EReloadResult>::Ok(EReloadResult::Success);
    }

    /**
     * @brief ReloadAtomicSwap - 简化热重载:建新 VM → load → 直接换 → 释放旧 VM。
     * @return load 失败时 Err;成功 Ok(Success)
     *
     * @detail 不做 drain、不迁移 actor state,in-flight 协程随旧 VM 一起销毁。
     */
    TResult<EReloadResult> MLuaHotReload::ReloadAtomicSwap(MLuaEngine& Engine, const MString& NewBytes) {
        auto NewState = std::make_unique<MLuaScriptState>();
        if (!NewState->IsValid()) {
            return TResult<EReloadResult>::Err(MString("ReloadAtomicSwap: new VM invalid"));
        }
        Engine.InstallStandardLibraries(*NewState);
        MString LoadErr = NewState->LoadBuffer("reload", NewBytes.c_str(), NewBytes.size());
        if (!LoadErr.empty()) {
            return TResult<EReloadResult>::Err(MString("ReloadAtomicSwap: load failed: ") + LoadErr);
        }
        // OldState 立即析构(丢状态);EndSwap 此时已是 no-op
        TUniquePtr<MLuaScriptState> OldState = Engine.BeginSwap(std::move(NewState));
        Engine.EndSwap();
        return TResult<EReloadResult>::Ok(EReloadResult::Success);
    }

} // namespace mession::script::lua