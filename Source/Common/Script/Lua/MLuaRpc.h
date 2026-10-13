/**
 * @file MLuaRpc.h
 * @brief MLuaRpc —— Lua 侧 RPC / Time / Id 工具桥接器声明。
 *
 * @detail 同时声明 RPC 桥接抽象接口 IScriptRpcBridge(由 MLuaEngine 实现)。
 */
#pragma once

#include "Common/Runtime/MLib.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    class IScriptRpcBridge; // forward decl — Engine 实现此接口

    /**
     * @brief MLuaRpc - Lua 侧的 RPC / Time / Id 工具桥接器。
     *
     * @detail Install 在 Mession 下建三个子表:
     *   - RPC:  call(name, ...) 经 bridge 按 Lua 全局函数名调用;
     *   - Time: now() 秒级时间戳、nowMs() 毫秒时间戳、sleepMs(ms) 阻塞休眠;
     *   - Id:   new() 自增唯一 ID。
     *   NOTE: RPC.callById 当前未在 .cpp 中注册。
     */
    class MLuaRpc {
        public:
        /**
         * @brief Install - 记录 bridge 并注册 Mession.RPC / Time / Id。
         * @param L       目标 Lua 状态机
         * @param pEngine RPC 桥接实现;必须存活到该 lua_State 关闭
         */
        static void Install(lua_State* L, IScriptRpcBridge* pEngine);
    };

    /**
     * @brief IScriptRpcBridge - RPC 调用桥接抽象接口(由 MLuaEngine 实现)。
     *
     * @detail 单独抽出该接口而不依赖 IScriptEngine.h,避免头文件耦合。
     */
    class IScriptRpcBridge {
        public:
        virtual ~IScriptRpcBridge() = default;

        /**
         * @brief CallGlobal - 按 Lua 全局函数名直接调用。
         * @param L          目标 Lua 状态机
         * @param GlobalName 全局函数名
         * @param ArgStart   实参起始栈索引(inclusive)
         * @param ArgEnd     实参结束栈索引(inclusive)
         * @return 结果值个数(0 = 无错误,被调函数的返回值已在栈上);出错时把错误消息 push 到栈顶
         */
        virtual int CallGlobal(lua_State* L, const char* GlobalName, int ArgStart, int ArgEnd) = 0;
    };

} // namespace mession::script::lua