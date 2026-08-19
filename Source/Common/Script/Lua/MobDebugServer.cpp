#include "Common/Script/Lua/MobDebugServer.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Script/Lua/LuaScriptState.h"

#include <chrono>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace mession::script::lua {

    namespace {

        // MobDebug hook:每 N 条指令触发一次,更新 per-engine 状态 + watchdog
        // engine pointer 通过 lua_getextraspace 获取(MLuaScriptState::SetOpaque 写入)
        // LUA_MASKCOUNT:每 Count 条指令触发;实际生产应 LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE
        constexpr int kHookCount = 1000;

        // Watchdog:60s 内无 hook 触发 → 自动关闭(防止生产环境 debug hook 卡死业务)
        constexpr uint64 kWatchdogMs = 60000;

        void DebugHook(lua_State* L, lua_Debug* /*Ar*/) {
            if (!L) return;

            // 通过 extraspace 拿 engine
            MLuaEngine* Engine = nullptr;
            void**       Extraspace = static_cast<void**>(lua_getextraspace(L));
            // 防 version mismatch:第一个 void* 是我们写入的 opaque
            // (Lua 5.4.7 LUA_EXTRASPACE = 20,前 sizeof(void*) 字节足够)
            Engine = static_cast<MLuaEngine*>(Extraspace[0]);
            if (!Engine) return;

            // 拿 per-engine debug state(指针)
            // 简化:MobDebug state 通过 Engine 内部的 DebugStatePtr 拿
            // (T11 加了字段;此处直接读)
            // 由于 FMobDebugState 在 MobDebugServer.h 定义且 MLuaEngine 持有指针,
            // 我们用 static_cast 解出指针后递增 InstructionCount
            auto* StatePtr = reinterpret_cast<FMobDebugState*>(Engine->GetDebugStatePtr());
            if (!StatePtr || !StatePtr->bRunning.load()) {
                // 已被 disable(可能在另一个 engine Disable 调用)— 摘 hook
                lua_sethook(L, nullptr, 0, 0);
                return;
            }

            // Watchdog 检查
            auto Now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            uint64 Last = StatePtr->LastHookTimestamp.load();
            if (Last != 0 && (Now - Last) > kWatchdogMs) {
                LOG_WARN("MobDebugServer: 60s watchdog triggered, disabling hook");
                lua_sethook(L, nullptr, 0, 0);
                StatePtr->bRunning.store(false);
                return;
            }
            StatePtr->LastHookTimestamp.store(Now);
            StatePtr->InstructionCount.fetch_add(1);
        }

    } // namespace

    void MLuaMobDebugServer::Enable(MLuaEngine& Engine, uint16 /*Port*/) {
        if (!Engine.GetStateForReload().IsValid()) {
            LOG_WARN("MobDebugServer: engine not initialized");
            return;
        }
        auto&      State = Engine.GetStateForReload();
        lua_State* L     = State.GetLuaState();

        // 创建 / 重用 per-engine FMobDebugState
        // (简化:由 MLuaEngine 在初始化时持有,这里只标记 bRunning=true)
        auto* StatePtr = reinterpret_cast<FMobDebugState*>(Engine.GetDebugStatePtr());
        if (!StatePtr) {
            LOG_ERROR("MobDebugServer: engine.DebugStatePtr is null");
            return;
        }
        StatePtr->InstructionCount.store(0);
        StatePtr->LastHookTimestamp.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        StatePtr->bRunning.store(true);

        // 注册 hook(粗粒度 LUA_MASKCOUNT 1000)
        lua_sethook(L, DebugHook, LUA_MASKCOUNT, kHookCount);

        LOG_INFO("MobDebugServer: hook enabled (engine opaque={})", (void*)L);
    }

    void MLuaMobDebugServer::Disable() {
        // Disable 需要知道当前挂哪个 VM — 简单做法:关闭 *所有* 已 enable 的 VM
        // 完整做法:维护一个 TSet<MLuaEngine*>,由 MLuaEngine::Shutdown 自动调用
        // 此简化版依赖 caller 在 Disable 之前知道 engine 指针:
        // (本文件不持有 engine 列表,所以提供单 engine 版本)
        // 业务侧用法:MLuaMobDebugServer::Disable() — 全局 disable
        //           MLuaEngine::GetDebugStatePtr() → FMobDebugState* → bRunning=false
        LOG_INFO("MobDebugServer: Disable called (no-op until next watchdog or explicit engine.Disable())");
    }

    // MLuaMobDebugServer 单 engine Disable — 业务侧拿到 engine 后调
    // 加在头文件里会破坏 namespace;放这里让 MLuaEngine 在 DualVM swap 时调
    void DisableForEngine(MLuaEngine& Engine) {
        auto* StatePtr = reinterpret_cast<FMobDebugState*>(Engine.GetDebugStatePtr());
        if (StatePtr) {
            StatePtr->bRunning.store(false);
        }
        if (Engine.GetStateForReload().IsValid()) {
            lua_State* L = Engine.GetStateForReload().GetLuaState();
            lua_sethook(L, nullptr, 0, 0);
        }
    }

} // namespace mession::script::lua