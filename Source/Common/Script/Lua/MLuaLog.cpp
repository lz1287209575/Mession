#include "Common/Script/Lua/MLuaLog.h"
#include "Common/Runtime/Log/Log.h"

namespace mession::script::lua {

    namespace {

        int LogAt(lua_State* L, ELogLevel Level) {
            // 契约: msg 必须是真 string(number 可隐式转 string,lua_isstring 会放行,
            // 但测试 TestLogTypeError 明确要求数字报错) —— 用 lua_type 严格判定。
            if (lua_type(L, 1) != LUA_TSTRING) {
                return luaL_error(L, "log: msg must be string");
            }
            size_t      Len = 0;
            const char* Msg = lua_tolstring(L, 1, &Len);
            MString     Text(Msg, Len);
            MLog::Write(LogLua, Level, "%s", Text.c_str());
            return 0;
        }

        /// @lua-stdlib Log.info
        /// @lua-param msg string
        /// @lua-return nil
        int LogInfo(lua_State* L) {
            return LogAt(L, ELogLevel::Info);
        }

        /// @lua-stdlib Log.warn
        /// @lua-param msg string
        /// @lua-return nil
        int LogWarn(lua_State* L) {
            return LogAt(L, ELogLevel::Warn);
        }

        /// @lua-stdlib Log.error
        /// @lua-param msg string
        /// @lua-return nil
        int LogError(lua_State* L) {
            return LogAt(L, ELogLevel::Error);
        }

        /// @lua-stdlib Log.debug
        /// @lua-param msg string
        /// @lua-return nil
        int LogDebug(lua_State* L) {
            return LogAt(L, ELogLevel::Debug);
        }

        /// @lua-stdlib Log.fatal
        /// @lua-param msg string
        /// @lua-return nil
        int LogFatal(lua_State* L) {
            return LogAt(L, ELogLevel::Critical);
        }

    } // namespace

    void MLuaLog::Install(lua_State* L) {
        lua_getglobal(L, "Mession");
        if (lua_isnil(L, -1)) {
            // 弹掉 getglobal 压入的 nil,再重建 Mession 表并复制引用:
            // setglobal 弹副本,栈上留下真表 —— 与表已存在分支栈净变化一致(+1),
            // 修复原实现残留 nil 导致的调用方栈索引错位。
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setglobal(L, "Mession");
        }
        lua_newtable(L);
        lua_pushcfunction(L, LogInfo);
        lua_setfield(L, -2, "info");
        lua_pushcfunction(L, LogWarn);
        lua_setfield(L, -2, "warn");
        lua_pushcfunction(L, LogError);
        lua_setfield(L, -2, "error");
        lua_pushcfunction(L, LogDebug);
        lua_setfield(L, -2, "debug");
        lua_pushcfunction(L, LogFatal);
        lua_setfield(L, -2, "fatal");
        lua_setfield(L, -2, "Log");
        lua_pop(L, 1);
    }

} // namespace mession::script::lua