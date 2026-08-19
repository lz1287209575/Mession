#include "Common/Script/Lua/LuaHotReload.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Script/Abstract/ScriptErrorCodes.h"
#include "Common/Script/Lua/FPendingCall.h"
#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Script/Lua/LuaScriptState.h"

#include <chrono>

namespace mession::script::lua {

    namespace {
        // Drain 阶段超时 — 30s 默认,可后续做成 SOption
        constexpr uint32 kDrainTimeoutSec = 30;
    }

    // ReloadDualVM_Real — 真 DualVM 热重载(9 阶段)
    //   1. drain in-flight 协程(StepCoroutines until PendingCalls==0 或 30s timeout)
    //   2. 失败 → fallback AtomicSwap
    //   3. 成功 → snapshot actor state(SaveAllActorStates)
    //   4. 创建 VM_B + InstallStandardLibraries(bootstrap)
    //   5. LoadBuffer 到 VM_B
    //   6. 失败 → return Err(load_failed);VM_A 仍 live,VmGeneration 未变
    //   7. 成功 → BeginSwap(VM_B) — bump VmGeneration,旧 VM 移到 PendingOldState
    //   8. RestoreAllActorStates(Snapshot)
    //   9. RebindCrossInstanceRefs — OnVmSwapped(OldH, NewH) 回调业务
    //  10. EndSwap — PendingOldState.reset() → lua_close 旧 VM
    TResult<EReloadResult> MLuaHotReload::ReloadDualVM(MLuaEngine& Engine, const MString& NewBytes) {
        // 1. Drain
        auto Drain = Engine.DrainPendingCalls(kDrainTimeoutSec);
        if (Drain.IsErr()) {
            LOG_WARN("ReloadDualVM: drain timeout ({}s), falling back to AtomicSwap", kDrainTimeoutSec);
            return ReloadAtomicSwap(Engine, NewBytes);
        }

        // 3. Snapshot actor state(读旧 VM,Before BeginSwap 拿写锁)
        auto Snapshot = Engine.SaveAllActorStates();

        // 4. Build VM_B
        auto NewState = std::make_unique<MLuaScriptState>();
        if (!NewState->IsValid()) {
            return TResult<EReloadResult>::Err(MString("ReloadDualVM: new VM invalid"));
        }

        // 5. Bootstrap stdlibs
        Engine.InstallStandardLibraries(*NewState);

        // 6. LoadBuffer
        MString LoadErr = NewState->LoadBuffer("reload", NewBytes.c_str(), NewBytes.size());
        if (!LoadErr.empty()) {
            return TResult<EReloadResult>::Err(MString("ReloadDualVM: load failed: ") + LoadErr);
        }

        // 7. BeginSwap(VM_A → PendingOldState,VM_B → State,bump VmGeneration)
        TUniquePtr<MLuaScriptState> OldState = Engine.BeginSwap(std::move(NewState));

        // 8. Restore actors in new VM
        Engine.RestoreAllActorStates(Snapshot);

        // 9. Rebind cross-instance refs
        Engine.RebindCrossInstanceRefs();

        // 10. EndSwap — 释放旧 VM
        Engine.EndSwap();
        // OldState 局部变量离开 scope → 触发 lua_close(再次确保)

        LOG_INFO("ReloadDualVM: complete, VmGeneration={}", Engine.GetVmGeneration());
        return TResult<EReloadResult>::Ok(EReloadResult::Success);
    }

    // ReloadAtomicSwap — 简化版:丢状态、in-flight 中断
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
        TUniquePtr<MLuaScriptState> OldState = Engine.BeginSwap(std::move(NewState));
        Engine.EndSwap();
        return TResult<EReloadResult>::Ok(EReloadResult::Success);
    }

} // namespace mession::script::lua