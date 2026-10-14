/**
 * @file LuaCoroutineBridge.cpp
 * @brief 实现协程 yield 判定、resume 与抛错三个薄封装。
 */
#include "Common/Script/Lua/LuaCoroutineBridge.h"
#include "Common/Runtime/Log/Log.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    /**
     * @brief IsCoroutineYielded - 线程是否处于 yield 挂起态。
     * @return true 表示 lua_status 为 LUA_YIELD;L 为空时 false
     */
    bool MLuaCoroutineBridge::IsCoroutineYielded(lua_State* L) {
        if (!L)
            return false;
        return lua_status(L) == LUA_YIELD;
    }

    /**
     * @brief ResumeFromStack - resume 协程并归类结果。
     * @return Invalid(L 为空)/ Error(实参区间不吻合或协程报错)/ Ok / Yield
     *
     * @detail 复杂度 O(1)。lua_resume 只消费栈顶连续 NArgs 个值,故校验实参区间
     *   [FromStackIdx, FromStackIdx+NArgs-1] 正好落在栈顶。Error 分支 pop 掉
     *   lua_resume 留在栈顶的错误值。
     */
    ECoroutineResumeStatus MLuaCoroutineBridge::ResumeFromStack(lua_State* L, int32 NArgs, int32 FromStackIdx) {
        if (!L) {
            return ECoroutineResumeStatus::Invalid;
        }

        // lua_resume 只把"栈顶连续 NArgs 个值"当实参,故首个实参必须满足
        // FromStackIdx + NArgs - 1 == 栈顶;否则 FromStackIdx 并未指向真正的实参起点。
        if (NArgs > 0) {
            int Top = lua_gettop(L);
            if (FromStackIdx < 1 || FromStackIdx + NArgs - 1 != Top) {
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
            // 非 Ok / Yield 一律视为错误,栈顶(若有)当作错误文本记录
            MString Err;
            if (lua_gettop(L) > 0 && lua_isstring(L, -1)) {
                const char* P = lua_tostring(L, -1);
                Err           = P ? MString(P) : MString("lua_resume_failed");
            } else {
                Err = MString("lua_resume_failed");
            }
            lua_pop(L, 1);
            LOG_ERROR("lua_resume failed: %s", Err.c_str());
            return ECoroutineResumeStatus::Error;
        }
        }
    }

    /**
     * @brief ThrowError - 压入 Msg 并抛 Lua 错误。
     * @param Msg 错误消息(按长度压栈,允许含嵌入 NUL)
     *
     * @detail 本函数不返回(不显式 return);调用点必须位于 Lua C 函数内。
     */
    void MLuaCoroutineBridge::ThrowError(lua_State* L, const MString& Msg) {
        lua_pushlstring(L, Msg.c_str(), Msg.size());
        lua_error(L);
    }

} // namespace mession::script::lua