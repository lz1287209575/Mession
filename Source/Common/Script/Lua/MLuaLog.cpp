#include "Common/Script/Lua/MLuaLog.h"
#include "Common/Runtime/Log/Log.h"

namespace mession::script::lua {

namespace {

int LogAt(lua_State* L, ELogLevel Level)
{
    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "log: msg must be string");
    }
    size_t      Len = 0;
    const char* Msg = lua_tolstring(L, 1, &Len);
    MString Text(Msg, Len);
    MLog::Write(LogLua, Level, "%s", Text.c_str());
    return 0;
}

int LogInfo(lua_State* L)  { return LogAt(L, ELogLevel::Info); }
int LogWarn(lua_State* L)  { return LogAt(L, ELogLevel::Warn); }
int LogError(lua_State* L) { return LogAt(L, ELogLevel::Error); }
int LogDebug(lua_State* L) { return LogAt(L, ELogLevel::Debug); }
int LogFatal(lua_State* L) { return LogAt(L, ELogLevel::Critical); }

} // namespace

void MLuaLog::Install(lua_State* L)
{
    lua_getglobal(L, "Mession");
    if (lua_isnil(L, -1)) {
        lua_newtable(L);
        lua_setglobal(L, "Mession");
        lua_getglobal(L, "Mession");
    }
    lua_newtable(L);
    lua_pushcfunction(L, LogInfo);  lua_setfield(L, -2, "info");
    lua_pushcfunction(L, LogWarn);  lua_setfield(L, -2, "warn");
    lua_pushcfunction(L, LogError); lua_setfield(L, -2, "error");
    lua_pushcfunction(L, LogDebug); lua_setfield(L, -2, "debug");
    lua_pushcfunction(L, LogFatal); lua_setfield(L, -2, "fatal");
    lua_setfield(L, -2, "Log");
    lua_pop(L, 1);
}

} // namespace mession::script::lua