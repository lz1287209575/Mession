#include "Common/Script/Lua/LuaCoroutineBridge.h"
#include "Common/Runtime/Log/Log.h"

namespace mession::script::lua {

bool MLuaCoroutineBridge::IsCoroutineYielded(lua_State* L)
{
    return lua_status(L) == LUA_YIELD;
}

void MLuaCoroutineBridge::ResumeFromStack(lua_State* L, int32 NResults)
{
    int NResultsOut = 0;
    int RC          = lua_resume(L, nullptr, 0, &NResultsOut);
    if (RC != LUA_OK && RC != LUA_YIELD)
    {
        MString Err = lua_tostring(L, -1);
        lua_pop(L, 1);
        LOG_ERROR("lua_resume failed: %s", Err.c_str());
    }
}

void MLuaCoroutineBridge::ThrowError(lua_State* L, const MString& Msg)
{
    lua_pushlstring(L, Msg.c_str(), Msg.size());
    lua_error(L);
}

} // namespace mession::script::lua