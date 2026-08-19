#include "Common/Script/Lua/MLuaRpc.h"
#include "Common/Runtime/Id.h"
#include "Common/Runtime/Time.h"

#include <cstring>

namespace mession::script::lua {

namespace {

IScriptRpcBridge* GBridge = nullptr;

/// @lua-stdlib RPC.call
/// @lua-param name string
/// @lua-param ... any
/// @lua-return any
/// @lua-return string
int RpcCall(lua_State* L)
{
    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "rpc.call: global_name must be string");
    }
    const char* Name = lua_tolstring(L, 1, nullptr);
    int Top = lua_gettop(L);
    if (!GBridge) {
        return luaL_error(L, "rpc: bridge not installed");
    }
    return GBridge->CallGlobal(L, Name, 2, Top);
}

/// @lua-stdlib Time.now
/// @lua-return number
int TimeNow(lua_State* L)
{
    lua_pushnumber(L, MTime::GetTimeSeconds());
    return 1;
}

/// @lua-stdlib Time.nowMs
/// @lua-return integer
int TimeNowMs(lua_State* L)
{
    auto Seconds = MTime::GetTimeSeconds();
    lua_pushinteger(L, static_cast<int64>(Seconds * 1000.0));
    return 1;
}

/// @lua-stdlib Time.sleepMs
/// @lua-param ms integer
/// @lua-return nil
int TimeSleepMs(lua_State* L)
{
    if (!lua_isnumber(L, 1)) {
        return luaL_error(L, "sleepMs: ms must be number");
    }
    int64 Ms = static_cast<int64>(lua_tointeger(L, 1));
    if (Ms > 0) {
        MTime::SleepMilliseconds(static_cast<uint32>(Ms));
    }
    return 0;
}

/// @lua-stdlib Id.new
/// @lua-return integer
int IdNew(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(MUniqueIdGenerator::Generate()));
    return 1;
}

} // namespace

void MLuaRpc::Install(lua_State* L, IScriptRpcBridge* pEngine)
{
    GBridge = pEngine;

    lua_getglobal(L, "Mession");
    if (lua_isnil(L, -1)) {
        lua_newtable(L);
        lua_setglobal(L, "Mession");
        lua_getglobal(L, "Mession");
    }

    // RPC namespace
    lua_newtable(L);
    lua_pushcfunction(L, RpcCall); lua_setfield(L, -2, "call");
    lua_setfield(L, -2, "RPC");

    // Time namespace
    lua_newtable(L);
    lua_pushcfunction(L, TimeNow);     lua_setfield(L, -2, "now");
    lua_pushcfunction(L, TimeNowMs);   lua_setfield(L, -2, "nowMs");
    lua_pushcfunction(L, TimeSleepMs); lua_setfield(L, -2, "sleepMs");
    lua_setfield(L, -2, "Time");

    // Id namespace
    lua_newtable(L);
    lua_pushcfunction(L, IdNew); lua_setfield(L, -2, "new");
    lua_setfield(L, -2, "Id");

    lua_pop(L, 1);
}

} // namespace mession::script::lua