/**
 * @file MobDebugServer.cpp
 * @brief 实现 Lua 指令级 hook:按 LUA_MASKCOUNT 采样、watchdog 自关、per-engine 摘除。
 */
#include "Common/Script/Lua/MobDebugServer.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Script/Lua/LuaScriptState.h"

#include <chrono>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    namespace {

        // hook 触发粒度:每 1000 条指令一次。
        // 生产环境若要覆盖更细的调试场景,应换成 LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE。
        constexpr int kHookCount = 1000;

        // Watchdog:距上次触发超过 60s 就自动关闭 hook,避免 debug hook 拖慢生产
        constexpr uint64 kWatchdogMs = 60000;

        /**
         * @brief DebugHook - LUA_MASKCOUNT 回调:更新计数并执行 watchdog。
         *
         * @detail 反向链路:VM extraspace 首槽存的是宿主 MLuaEngine*(由
         *   MLuaScriptState::SetOpaque 写入),由此拿到 per-engine 的 FMobDebugState。
         *   bRunning 为假或自身为空时直接把 hook 摘掉。时间用 steady_clock,
         *   因此 LastHookTimestamp 不是墙上时间戳。
         */
        void DebugHook(lua_State* L, lua_Debug* /*Ar*/) {
            if (!L)
                return;

            // 经 extraspace 反查宿主 engine
            MLuaEngine* Engine     = nullptr;
            void**      Extraspace = static_cast<void**>(lua_getextraspace(L));
            // 首槽即 SetOpaque 写入的 opaque(LUA_EXTRASPACE == sizeof(void*) 恰好容纳)
            Engine = static_cast<MLuaEngine*>(Extraspace[0]);
            if (!Engine)
                return;

            // 取本 engine 的 hook 状态(可能为空,例如引擎已析构)
            auto* StatePtr = reinterpret_cast<FMobDebugState*>(Engine->GetDebugStatePtr());
            if (!StatePtr || !StatePtr->bRunning.load()) {
                // 已被 disable —— 自我摘除
                lua_sethook(L, nullptr, 0, 0);
                return;
            }

            // Watchdog:距上次触发超过 60s 视为卡死
            auto   Now  = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
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

    /**
     * @brief Enable - 挂 hook 并置 bRunning。
     * @param Port 预留参数,当前实现忽略
     *
     * @detail 复用 MLuaEngine 在构造时就持有的 FMobDebugState,这里只做计数清零、
     *   时间戳初始化和 bRunning 置位;随后 lua_sethook(LUA_MASKCOUNT)。
     */
    void MLuaMobDebugServer::Enable(MLuaEngine& Engine, uint16 /*Port*/) {
        if (!Engine.GetStateForReload().IsValid()) {
            LOG_WARN("MobDebugServer: engine not initialized");
            return;
        }
        auto&      State = Engine.GetStateForReload();
        lua_State* L     = State.GetLuaState();

        // 复用 MLuaEngine 已持有的 per-engine 状态,这里只置标志
        auto* StatePtr = reinterpret_cast<FMobDebugState*>(Engine.GetDebugStatePtr());
        if (!StatePtr) {
            LOG_ERROR("MobDebugServer: engine.DebugStatePtr is null");
            return;
        }
        StatePtr->InstructionCount.store(0);
        StatePtr->LastHookTimestamp.store(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        StatePtr->bRunning.store(true);

        // 挂粗粒度 hook(LUA_MASKCOUNT,每 1000 条指令一次)
        lua_sethook(L, DebugHook, LUA_MASKCOUNT, kHookCount);

        LOG_INFO("MobDebugServer: hook enabled (engine opaque={})", (void*)L);
    }

    /**
     * @brief Disable - 当前为 no-op(只记日志)。
     *
     * @detail 本实现不维护 engine 列表,无法得知该摘哪个 VM 的 hook。
     *   完整做法是维护 TSet<MLuaEngine*>,由 MLuaEngine::Shutdown 自动调用。
     */
    void MLuaMobDebugServer::Disable() {
        LOG_INFO("MobDebugServer: Disable called (no-op until next watchdog or explicit engine.Disable())");
    }

    /**
     * @brief DisableForEngine - 摘掉指定 engine 的 hook。
     *
     * @detail 先把 bRunning 置 false(让残留 hook 回调自我摘除),再对当前活跃 VM
     *   调 lua_sethook 解绑。VM 无效时只置标志。
     */
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