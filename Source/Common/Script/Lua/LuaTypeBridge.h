/**
 * @file LuaTypeBridge.h
 * @brief LuaTypeBridge —— Lua 栈的标量 push / pop / 类型判定自由函数集。
 *
 * @detail 全部是无状态自由函数,只做一层薄转换与类型校验;PopXxx 失败返回 Err
 *   而不是抛错。索引沿用 Lua 的 1-based / 负值相对约定。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    /** @brief PushInteger - 压入一个 Lua integer(由 int64 截断为 lua_Integer) */
    void PushInteger(lua_State* L, int64 Value);
    /** @brief PushBoolean - 压入一个 Lua boolean */
    void PushBoolean(lua_State* L, bool Value);
    /** @brief PushNumber - 压入一个 Lua float */
    void PushNumber(lua_State* L, double Value);
    /** @brief PushString - 按长度压入字符串(允许含嵌入 NUL) */
    void PushString(lua_State* L, const MString& Value);
    /** @brief PushNil - 压入 nil */
    void PushNil(lua_State* L);

    /** @brief PopInteger - 取整数;非 Lua number 时返回 Err(不 pop,只读) */
    TResult<int64> PopInteger(lua_State* L, int32 Index);
    /** @brief PopBoolean - 取布尔;类型不符返回 Err */
    TResult<bool> PopBoolean(lua_State* L, int32 Index);
    /** @brief PopNumber - 取 double;类型不符返回 Err */
    TResult<double> PopNumber(lua_State* L, int32 Index);
    /** @brief PopString - 取字符串(拷贝);类型不符返回 Err */
    TResult<MString> PopString(lua_State* L, int32 Index);

    /**
     * @brief IsInteger - 判定是否为 Lua integer 子类型(浮点数返回 false)。
     *
     * @detail 用 lua_isinteger 区分 integer / float 两个 number 子类型。
     */
    bool IsInteger(lua_State* L, int32 Index);
    /** @brief IsBoolean - 判定是否为 Lua boolean */
    bool IsBoolean(lua_State* L, int32 Index);
    /** @brief IsNumber - 判定是否为 Lua number(integer 与 float 都返回 true) */
    bool IsNumber(lua_State* L, int32 Index);
    /** @brief IsString - 判定是否为 Lua string(number 不会隐式通过,不同于 lua_isstring) */
    bool IsString(lua_State* L, int32 Index);
    /** @brief IsNil - 判定是否为 Lua nil */
    bool IsNil(lua_State* L, int32 Index);

} // namespace mession::script::lua