#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/Log.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace mession::script::lua {

// MLuaLog — Lua → MLog 桥接器
// 用法:MLuaLog::Install(L) 注册 Mession.Log.{info, warn, error, debug, fatal}(msg)
// 内部:每个函数走 MLog::Write(LogLua, ELogLevel, "{msg}")
// 所有 Lua 端日志统一走 LogLua category
class MLuaLog
{
public:
    static void Install(lua_State* L);
};

} // namespace mession::script::lua