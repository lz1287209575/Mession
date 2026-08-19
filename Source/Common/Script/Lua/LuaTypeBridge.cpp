#include "Common/Script/Lua/LuaTypeBridge.h"

namespace mession::script::lua {

    void PushInteger(lua_State* L, int64 Value) {
        lua_pushinteger(L, static_cast<lua_Integer>(Value));
    }
    void PushBoolean(lua_State* L, bool Value) {
        lua_pushboolean(L, Value);
    }
    void PushNumber(lua_State* L, double Value) {
        lua_pushnumber(L, Value);
    }
    void PushString(lua_State* L, const MString& Value) {
        lua_pushlstring(L, Value.c_str(), Value.size());
    }
    void PushNil(lua_State* L) {
        lua_pushnil(L);
    }

    bool IsInteger(lua_State* L, int32 Index) {
        return lua_type(L, Index) == LUA_TNUMBER;
    }
    bool IsBoolean(lua_State* L, int32 Index) {
        return lua_type(L, Index) == LUA_TBOOLEAN;
    }
    bool IsNumber(lua_State* L, int32 Index) {
        return lua_type(L, Index) == LUA_TNUMBER;
    }
    bool IsString(lua_State* L, int32 Index) {
        return lua_type(L, Index) == LUA_TSTRING;
    }
    bool IsNil(lua_State* L, int32 Index) {
        return lua_type(L, Index) == LUA_TNIL;
    }

    TResult<int64> PopInteger(lua_State* L, int32 Index) {
        if (!IsInteger(L, Index)) {
            return TResult<int64>::Err(MString("expected integer at stack"));
        }
        return TResult<int64>::Ok(static_cast<int64>(lua_tointeger(L, Index)));
    }

    TResult<bool> PopBoolean(lua_State* L, int32 Index) {
        if (!IsBoolean(L, Index)) {
            return TResult<bool>::Err(MString("expected boolean at stack"));
        }
        return TResult<bool>::Ok(lua_toboolean(L, Index) != 0);
    }

    TResult<double> PopNumber(lua_State* L, int32 Index) {
        if (!IsNumber(L, Index)) {
            return TResult<double>::Err(MString("expected number at stack"));
        }
        return TResult<double>::Ok(lua_tonumber(L, Index));
    }

    TResult<MString> PopString(lua_State* L, int32 Index) {
        if (!IsString(L, Index)) {
            return TResult<MString>::Err(MString("expected string at stack"));
        }
        size_t      Len = 0;
        const char* P   = lua_tolstring(L, Index, &Len);
        return TResult<MString>::Ok(MString(P, Len));
    }

} // namespace mession::script::lua