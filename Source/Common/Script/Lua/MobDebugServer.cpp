#include "Common/Script/Lua/MobDebugServer.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Script/Lua/LuaEngine.h"

#include <atomic>
#include <chrono>
#include <thread>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    namespace {

        struct FMobDebugState {
            std::atomic<bool>   bRunning{false};
            std::atomic<uint32> InstructionCount{0};
            std::atomic<uint64> LastHookTimestamp{0}; // ms since epoch
        };

        FMobDebugState GDebugState;

        void DebugHook(lua_State* L, lua_Debug* Ar) {
            GDebugState.InstructionCount.fetch_add(1);

            auto Now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

            // Watchdog:60s 未动 → 关掉 debug
            if (Now - GDebugState.LastHookTimestamp.load() > 60000) {
                LOG_WARN("MobDebugServer: 60s watchdog triggered, disabling hook");
                lua_sethook(L, nullptr, 0, 0);
                GDebugState.bRunning.store(false);
                return;
            }
            GDebugState.LastHookTimestamp.store(Now);

            (void)Ar;
        }

    } // namespace

    void MLuaMobDebugServer::Enable(MLuaEngine& Engine, uint16 Port) {
        if (GDebugState.bRunning.load()) {
            LOG_WARN("MobDebugServer: already enabled");
            return;
        }

        // 注册 lua_sethook:每 1000 条指令触发一次(粗粒度,生产用更细)
        // 真实生产应 mobdebug.lua + IDE 双向 TCP — 本简化版仅本地 hook
        auto&      State = Engine.GetStateForReload(); // 复用 MLuaEngine accessor
        lua_State* L     = State.GetLuaState();
        if (L) {
            GDebugState.LastHookTimestamp.store(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
            lua_sethook(L, DebugHook, LUA_MASKCOUNT, 1000);
            GDebugState.bRunning.store(true);
            LOG_INFO("MobDebugServer: hook enabled on port {}", Port);
        } else {
            LOG_ERROR("MobDebugServer: engine state invalid");
        }

        // 占位:真实实现需独立 TCP 监听线程 + mobdebug.lua require + IDE 协议
        // TODO:Task 11 完整集成 — 启动 std::thread 监听 0.0.0.0:Port,
        //      解析 MobDebug TCP 协议,转发到 lua_sethook 控制断点
    }

    void MLuaMobDebugServer::Disable() {
        GDebugState.bRunning.store(false);
        LOG_INFO("MobDebugServer: disabled");
    }

} // namespace mession::script::lua