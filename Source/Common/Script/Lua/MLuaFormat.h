#pragma once

#include "Common/Runtime/MLib.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace mession::script::lua {

// MLuaFormat — Lua 字符串格式化桥接器
// 用法:MLuaFormat::Install(L) 注册 Mession.Format.{fmt, concat, split, tostring, trim}
// 内部 fmt 走 MFormat::Format(template, args...)(fmt-style {} 占位符)
class MLuaFormat
{
public:
    static void Install(lua_State* L);
};

} // namespace mession::script::lua