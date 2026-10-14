/**
 * @file LuaCoroutineBridge.h
 * @brief MLuaCoroutineBridge / ECoroutineResumeStatus —— 协程 resume 的薄封装。
 *
 * @detail 只负责"把栈上的值当 resume 实参送进协程"以及状态归类,不负责挂起登记与
 *   调度(那是 FLuaPendingCallRegistry / MLuaEngine::StepCoroutines 的职责)。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Lua/FPendingCall.h"

extern "C" {
#include <lua.h>
}

namespace mession::script::lua {

    /**
     * @brief ECoroutineResumeStatus - ResumeFromStack 的结果归类。
     */
    enum class ECoroutineResumeStatus : uint8 {
        Ok      = 0, // 协程正常返回(已结束)
        Yield   = 1, // 协程再次 yield,仍挂起
        Error   = 2, // 协程报错,或栈上实参不足
        Invalid = 3, // lua_State 为空/无效
    };

    /**
     * @brief MLuaCoroutineBridge - 协程 resume 的静态工具。
     */
    class MLuaCoroutineBridge {
        public:
        /**
         * @brief IsCoroutineYielded - 判断线程是否处于 yield 挂起态。
         * @return L->status() == LUA_YIELD;L 为空返回 false
         */
        static bool IsCoroutineYielded(lua_State* L);
        /**
         * @brief ResumeFromStack - 以栈上已有值作为实参 resume。
         * @param L           协程的 lua_State*
         * @param NArgs       实参个数(取自栈顶连续 NArgs 个值)
         * @param FromStackIdx 首个实参的栈索引,不会搬动栈上元素
         * @return 状态归类;实参区间与栈顶不吻合时返回 Error
         *
         * @detail lua_resume 只把"栈顶连续 NArgs 个值"当实参,故要求实参区间正好落在
         *   栈顶(FromStackIdx + NArgs - 1 == lua_gettop(L));FromStackIdx 不匹配则返回
         *   Error。错误分支 pop 掉 lua_resume 留在栈顶的错误值。
         */
        static ECoroutineResumeStatus ResumeFromStack(lua_State* L, int32 NArgs, int32 FromStackIdx = 1);
        /** @brief ThrowError - 把 Msg 压栈并 lua_error(不返回) */
        static void ThrowError(lua_State* L, const MString& Msg);
    };

} // namespace mession::script::lua