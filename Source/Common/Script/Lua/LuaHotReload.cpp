#include "Common/Script/Lua/LuaHotReload.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Script/Lua/FPendingCall.h"
#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Script/Lua/LuaScriptState.h"

#include <chrono>

namespace mession::script::lua {

    namespace {

        // 从 Old VM 统计 in-flight 协程数(简化:通过 lua_State 注册表或栈顶统计)
        // 真实路径:遍历 FLuaPendingCall 实例。本简化版返回 -1 表示"无法统计,跳过 drain"。
        int CountPendingCalls(MLuaScriptState& /*State*/) {
            // TODO:维护一个全局 FLuaPendingCall registry 跟踪
            // 当前简化:返回 -1 表示"未知,跳过 drain 直接 AtomicSwap"
            return -1;
        }

    } // namespace

    TResult<EReloadResult> MLuaHotReload::ReloadDualVM(MLuaEngine& Engine, const MString& NewBytes) {
        constexpr int TimeoutSeconds = 30;
        auto          StartTime      = std::chrono::steady_clock::now();

        // 1. 统计旧 VM 的 in-flight 协程数
        int Pending = CountPendingCalls(Engine.GetStateForReload());
        if (Pending < 0) {
            // 无法统计 — fallback 到 AtomicSwap
            LOG_WARN("ReloadDualVM: cannot count pending calls, falling back to AtomicSwap");
            return ReloadAtomicSwap(Engine, NewBytes);
        }

        // 2. 等所有 in-flight 完成,带 30s 超时
        while (Pending > 0) {
            Engine.StepCoroutines(); // 让协程调度
            auto Elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - StartTime).count();
            if (Elapsed >= TimeoutSeconds) {
                LOG_WARN("ReloadDualVM: timeout ({}s), falling back to AtomicSwap", Elapsed);
                return ReloadAtomicSwap(Engine, NewBytes);
            }
            Pending = CountPendingCalls(Engine.GetStateForReload());
        }

        // 3. 所有 in-flight 完成 — 创建 NewVM + 加载 NewBytes
        auto NewState = std::make_unique<MLuaScriptState>();
        if (!NewState->IsValid()) {
            return TResult<EReloadResult>::Err(MString("ReloadDualVM: new VM invalid"));
        }
        MString LoadErr = NewState->LoadBuffer("reload", NewBytes.c_str(), NewBytes.size());
        if (!LoadErr.empty()) {
            return TResult<EReloadResult>::Err(MString("ReloadDualVM: load failed: ") + LoadErr);
        }

        // 4. 替换 State,旧 State 离开 scope 自动 lua_close
        TUniquePtr<MLuaScriptState> OldState = Engine.ReplaceState(std::move(NewState));

        return TResult<EReloadResult>::Ok(EReloadResult::Success);
    }

    TResult<EReloadResult> MLuaHotReload::ReloadAtomicSwap(MLuaEngine& Engine, const MString& NewBytes) {
        // 直接创建新 VM + 替换,接受 in-flight 协程中断
        auto NewState = std::make_unique<MLuaScriptState>();
        if (!NewState->IsValid()) {
            return TResult<EReloadResult>::Err(MString("ReloadAtomicSwap: new VM invalid"));
        }
        MString LoadErr = NewState->LoadBuffer("reload", NewBytes.c_str(), NewBytes.size());
        if (!LoadErr.empty()) {
            return TResult<EReloadResult>::Err(MString("ReloadAtomicSwap: load failed: ") + LoadErr);
        }
        Engine.ReplaceState(std::move(NewState));
        return TResult<EReloadResult>::Ok(EReloadResult::Success);
    }

} // namespace mession::script::lua