/**
 * @file MLuaLog.cpp
 * @brief 实现 MLuaLog:Mession.Log.* 五个入口与 Mession.Log 表注册。
 */
#include "Common/Script/Lua/MLuaLog.h"
#include "Common/Runtime/Log/Log.h"

namespace mession::script::lua {

    namespace {

        /**
         * @brief LogAt - 取 1 号槽位的 msg,按 Level 写入 LogLua category。
         * @param L     目标 Lua 状态机
         * @param Level 目标日志级别
         * @return 0(不向 Lua 返回值;类型不符时经 luaL_error 抛错)
         *
         * @detail 契约: msg 必须是真 string。number 可隐式转 string 且 lua_isstring
         *   会放行,但测试 TestLogTypeError 明确要求数字报错,故用 lua_type 严格判定。
         */
        int LogAt(lua_State* L, ELogLevel Level) {
            if (lua_type(L, 1) != LUA_TSTRING) {
                return luaL_error(L, "log: msg must be string");
            }
            size_t      Len = 0;
            const char* Msg = lua_tolstring(L, 1, &Len);
            MString     Text(Msg, Len);
            MLog::Write(LogLua, Level, "%s", Text.c_str());
            return 0;
        }

        /** @brief LogInfo - 记录 Info 级日志(msg 必须为 string,否则抛错) */
        /// @lua-stdlib Log.info
        /// @lua-param msg string
        /// @lua-return nil
        int LogInfo(lua_State* L) {
            return LogAt(L, ELogLevel::Info);
        }

        /** @brief LogWarn - 记录 Warn 级日志(msg 必须为 string,否则抛错) */
        /// @lua-stdlib Log.warn
        /// @lua-param msg string
        /// @lua-return nil
        int LogWarn(lua_State* L) {
            return LogAt(L, ELogLevel::Warn);
        }

        /** @brief LogError - 记录 Error 级日志(msg 必须为 string,否则抛错) */
        /// @lua-stdlib Log.error
        /// @lua-param msg string
        /// @lua-return nil
        int LogError(lua_State* L) {
            return LogAt(L, ELogLevel::Error);
        }

        /** @brief LogDebug - 记录 Debug 级日志(msg 必须为 string,否则抛错) */
        /// @lua-stdlib Log.debug
        /// @lua-param msg string
        /// @lua-return nil
        int LogDebug(lua_State* L) {
            return LogAt(L, ELogLevel::Debug);
        }

        /** @brief LogFatal - 记录 Critical 级日志(msg 必须为 string,否则抛错) */
        /// @lua-stdlib Log.fatal
        /// @lua-param msg string
        /// @lua-return nil
        int LogFatal(lua_State* L) {
            return LogAt(L, ELogLevel::Critical);
        }

    } // namespace

    /**
     * @brief Install - 取/建 Mession 全局表,再把 Log 子表挂上去。
     * @param L 目标 Lua 状态机
     */
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