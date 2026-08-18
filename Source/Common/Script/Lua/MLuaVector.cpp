#include "Common/Script/Lua/MLuaVector.h"
#include "Common/Runtime/Log/Log.h"

namespace mession::script::lua {

TResult<MScalarValue> MScalarValue::FromLua(lua_State* L, int32 Index)
{
    int Type = lua_type(L, Index);
    switch (Type)
    {
    case LUA_TNIL:
        return TResult<MScalarValue>::Ok(MScalarValue::MakeInt(0));  // nil → 0
    case LUA_TBOOLEAN:
        return TResult<MScalarValue>::Ok(MScalarValue::MakeInt(lua_toboolean(L, Index) ? 1 : 0));
    case LUA_TNUMBER:
        // Lua 5.4 数字可同时是整数/浮点;这里走 double(更通用)
        return TResult<MScalarValue>::Ok(MScalarValue::MakeDouble(lua_tonumber(L, Index)));
    case LUA_TSTRING:
    {
        size_t Len = 0;
        const char* P = lua_tolstring(L, Index, &Len);
        return TResult<MScalarValue>::Ok(MScalarValue::MakeString(MString(P, Len)));
    }
    default:
        return TResult<MScalarValue>::Err(MString("unsupported_type"));
    }
}

void MScalarValue::PushToLua(lua_State* L) const
{
    switch (Type)
    {
    case MScalarType::Int:
        lua_pushinteger(L, IntVal);
        break;
    case MScalarType::Double:
        lua_pushnumber(L, DoubleVal);
        break;
    case MScalarType::String:
        lua_pushlstring(L, StringVal.c_str(), StringVal.size());
        break;
    }
}

MString MScalarValue::ToDebugString() const
{
    switch (Type)
    {
    case MScalarType::Int:    return MString("Int(") + std::to_string(IntVal) + MString(")");
    case MScalarType::Double: return MString("Double(") + std::to_string(DoubleVal) + MString(")");
    case MScalarType::String: return MString("String(") + StringVal + MString(")");
    }
    return MString("Unknown");
}

} // namespace mession::script::lua