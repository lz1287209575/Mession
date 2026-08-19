#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    void PushInteger(lua_State* L, int64 Value);
    void PushBoolean(lua_State* L, bool Value);
    void PushNumber(lua_State* L, double Value);
    void PushString(lua_State* L, const MString& Value);
    void PushNil(lua_State* L);

    TResult<int64>   PopInteger(lua_State* L, int32 Index);
    TResult<bool>    PopBoolean(lua_State* L, int32 Index);
    TResult<double>  PopNumber(lua_State* L, int32 Index);
    TResult<MString> PopString(lua_State* L, int32 Index);

    bool IsInteger(lua_State* L, int32 Index);
    bool IsBoolean(lua_State* L, int32 Index);
    bool IsNumber(lua_State* L, int32 Index);
    bool IsString(lua_State* L, int32 Index);
    bool IsNil(lua_State* L, int32 Index);

} // namespace mession::script::lua