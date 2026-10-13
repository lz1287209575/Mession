/**
 * @file MLuaRpc.cpp
 * @brief 实现 MLuaRpc:Mession.RPC / Time / Id 三个子表与 RPC 桥接调用。
 */
#include "Common/Script/Lua/MLuaRpc.h"
#include "Common/Runtime/Id.h"
#include "Common/Runtime/Time.h"

#include <cstring>

namespace mession::script::lua {

    namespace {

        /**
         * @brief GBridge - 当前进程内安装的 RPC bridge(文件级静态变量)。
         *
         * @detail NOTE: 位于匿名命名空间、进程级共享 —— 后一次 Install 会覆盖前一次,
         *   多 engine 共存时不隔离;且无任何同步保护,跨线程使用不安全。
         */
        IScriptRpcBridge* GBridge = nullptr;

        /** @brief RpcCall - 按 Lua 全局函数名经 bridge 调用 */
        /// @lua-stdlib RPC.call
        /// @lua-param name string
        /// @lua-param ... any
        /// @lua-return any
        /// @lua-return string
        int RpcCall(lua_State* L) {
            if (!lua_isstring(L, 1)) {
                return luaL_error(L, "rpc.call: global_name must be string");
            }
            const char* Name = lua_tolstring(L, 1, nullptr);
            int         Top  = lua_gettop(L);
            if (!GBridge) {
                return luaL_error(L, "rpc: bridge not installed");
            }
            return GBridge->CallGlobal(L, Name, 2, Top);
        }

        /** @brief TimeNow - 返回当前时间戳(秒,double) */
        /// @lua-stdlib Time.now
        /// @lua-return number
        int TimeNow(lua_State* L) {
            lua_pushnumber(L, MTime::GetTimeSeconds());
            return 1;
        }

        /** @brief TimeNowMs - 返回当前时间戳(毫秒,integer) */
        /// @lua-stdlib Time.nowMs
        /// @lua-return integer
        int TimeNowMs(lua_State* L) {
            auto Seconds = MTime::GetTimeSeconds();
            lua_pushinteger(L, static_cast<int64>(Seconds * 1000.0));
            return 1;
        }

        /** @brief TimeSleepMs - 阻塞休眠指定毫秒数 */
        /// @lua-stdlib Time.sleepMs
        /// @lua-param ms integer
        /// @lua-return nil
        int TimeSleepMs(lua_State* L) {
            if (!lua_isnumber(L, 1)) {
                return luaL_error(L, "sleepMs: ms must be number");
            }
            int64 Ms = static_cast<int64>(lua_tointeger(L, 1));
            if (Ms > 0) {
                MTime::SleepMilliseconds(static_cast<uint32>(Ms));
            }
            return 0;
        }

        /** @brief IdNew - 生成一个自增唯一 ID */
        /// @lua-stdlib Id.new
        /// @lua-return integer
        int IdNew(lua_State* L) {
            lua_pushinteger(L, static_cast<lua_Integer>(MUniqueIdGenerator::Generate()));
            return 1;
        }

    } // namespace

    /**
     * @brief Install - 记录 bridge 并注册 Mession.RPC / Time / Id 三个子表。
     * @param L       目标 Lua 状态机
     * @param pEngine RPC 桥接实现
     *
     * @detail pEngine 写入文件级静态变量 GBridge(见上),同一进程内以最后一次安装为准。
     */
    void MLuaRpc::Install(lua_State* L, IScriptRpcBridge* pEngine) {
        GBridge = pEngine;

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

        // RPC 命名空间
        lua_newtable(L);
        lua_pushcfunction(L, RpcCall);
        lua_setfield(L, -2, "call");
        lua_setfield(L, -2, "RPC");

        // Time 命名空间
        lua_newtable(L);
        lua_pushcfunction(L, TimeNow);
        lua_setfield(L, -2, "now");
        lua_pushcfunction(L, TimeNowMs);
        lua_setfield(L, -2, "nowMs");
        lua_pushcfunction(L, TimeSleepMs);
        lua_setfield(L, -2, "sleepMs");
        lua_setfield(L, -2, "Time");

        // Id 命名空间
        lua_newtable(L);
        lua_pushcfunction(L, IdNew);
        lua_setfield(L, -2, "new");
        lua_setfield(L, -2, "Id");

        lua_pop(L, 1);
    }

} // namespace mession::script::lua