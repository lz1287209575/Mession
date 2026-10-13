/**
 * @file MLuaLog.h
 * @brief MLuaLog —— Lua 侧日志桥接器声明。
 *
 * @detail Install 在 Lua 全局表 Mession 下建 Log 子表,暴露
 *   info / warn / error / debug / fatal 五个入口;实现见 MLuaLog.cpp。
 */
#pragma once

#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/MLib.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    /**
     * @brief MLuaLog - 把 Lua 的 Mession.Log.* 调用转发到 MLog。
     *
     * @detail 所有 Lua 端日志统一走 LogLua category;每个入口最终都落回
     *   MLog::Write(LogLua, ELogLevel, "%s", msg)。
     */
    class MLuaLog {
        public:
        /**
         * @brief Install - 在当前 VM 上注册 Mession.Log.{info, warn, error, debug, fatal}。
         * @param L 目标 Lua 状态机
         *
         * @detail 幂等:重复调用只覆盖同名 field。
         */
        static void Install(lua_State* L);
    };

} // namespace mession::script::lua