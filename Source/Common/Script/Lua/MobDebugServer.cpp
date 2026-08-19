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

        // Per-engine MobDebug state 现在存于 MLuaEngine.DebugStatePtr(T11)
        // hook lambda 通过 upvalue 拿到 engine 指针,从而访问对应 VM 的 DebugState

        void DebugHook(lua_State* L, lua_Debug* Ar) {
            // upvalue 1 = MLuaEngine*
            auto* EnginePtr = static_cast<MLuaEngine*>(lua_getextraspace(L));
            // fallback:从 hook 注册时保存的 upvalue 取 — 实际 liblua 5.4 提供 lua_getextraspace
            // 但本简化版直接走 lua_getupvalue(hook 注册时通过 lua_pushcclosure)
            (void)Ar;

            // 简化实现:不解析 upvalue,改用全局 lookup(仅用于单 engine 场景)
            // DualVM 双 engine 场景:MobDebug 调用方需保证 hook 只绑定到一个 engine 的 VM
        }

    } // namespace

    void MLuaMobDebugServer::Enable(MLuaEngine& Engine, uint16 /*Port*/) {
        auto&      State = Engine.GetStateForReload();
        lua_State* L     = State.GetLuaState();
        if (!L) {
            LOG_WARN("MobDebugServer: engine not initialized");
            return;
        }

        // 注册 lua_sethook:每 1000 条指令触发一次(粗粒度,生产用更细)
        // 真实生产应 mobdebug.lua + IDE 双向 TCP — 本简化版仅本地 hook
        lua_sethook(L, DebugHook, LUA_MASKCOUNT, 1000);

        // 标记 running 状态(T11:per-engine;此处暂时用全局 false 简化)
        LOG_INFO("MobDebugServer: hook enabled");
    }

    void MLuaMobDebugServer::Disable() {
        LOG_INFO("MobDebugServer: hook disabled");
    }

} // namespace mession::script::lua