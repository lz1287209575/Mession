#include "Common/Script/Lua/LuaCoroutineBridge.h"
#include "Common/Runtime/Log/Log.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    bool MLuaCoroutineBridge::IsCoroutineYielded(lua_State* L) {
        if (!L) return false;
        return lua_status(L) == LUA_YIELD;
    }

    ECoroutineResumeStatus MLuaCoroutineBridge::ResumeFromStack(lua_State* L, int32 NArgs, int32 FromStackIdx) {
        if (!L) {
            return ECoroutineResumeStatus::Invalid;
        }

        // 校验栈上确实有 NArgs 个可用值
        if (NArgs > 0) {
            int Top = lua_gettop(L);
            if (Top < FromStackIdx + NArgs - 1) {
                return ECoroutineResumeStatus::Error;
            }
        }

        int NResultsOut = 0;
        int RC          = lua_resume(L, nullptr, NArgs, &NResultsOut);

        switch (RC) {
        case LUA_OK:
            return ECoroutineResumeStatus::Ok;
        case LUA_YIELD:
            return ECoroutineResumeStatus::Yield;
        default: {
            MString Err;
            if (lua_gettop(L) > 0 && lua_isstring(L, -1)) {
                const char* P = lua_tostring(L, -1);
                Err            = P ? MString(P) : MString("lua_resume_failed");
            } else {
                Err = MString("lua_resume_failed");
            }
            lua_pop(L, 1);
            LOG_ERROR("lua_resume failed: %s", Err.c_str());
            return ECoroutineResumeStatus::Error;
        }
        }
    }

    void MLuaCoroutineBridge::ThrowError(lua_State* L, const MString& Msg) {
        lua_pushlstring(L, Msg.c_str(), Msg.size());
        lua_error(L);
    }

} // namespace mession::script::lua