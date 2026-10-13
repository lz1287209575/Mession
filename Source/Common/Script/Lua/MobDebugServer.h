/**
 * @file MobDebugServer.h
 * @brief FMobDebugState / MLuaMobDebugServer —— Lua 指令级 hook 的开关与观测状态。
 *
 * @detail 状态从早期的文件级 static 抬成 per-engine,避免两个 MLuaEngine 并存时
 *   互相覆盖。hook 本身用 LUA_MASKCOUNT 粗粒度触发,不做真正的 MobDebug 协议。
 */
#pragma once

#include "Common/Runtime/MLib.h"

#include <atomic>

namespace mession::script::lua {

    class MLuaEngine;

    /**
     * @brief FMobDebugState - per-engine 的 hook 运行状态。
     *
     * @detail 三个字段都是 std::atomic,可被 hook 回调(执行 Lua 的线程)与外部线程
     *   同时读写。原先挂在文件级 static 上,双引擎并存会互相覆盖,故下放到 MLuaEngine。
     */
    struct FMobDebugState {
        std::atomic<bool>   bRunning{false};      // false 时 hook 自我摘除
        std::atomic<uint32> InstructionCount{0};  // 累计触发次数;Enable 时清零
        std::atomic<uint64> LastHookTimestamp{0}; // 上次触发时刻:steady_clock 毫秒(非墙上时钟)
    };

    /**
     * @brief MLuaMobDebugServer - hook 的开启 / 关闭入口。
     */
    class MLuaMobDebugServer {
        public:
        /**
         * @brief Enable - 在 Engine 当前 VM 上挂 LUA_MASKCOUNT hook。
         * @param Engine 目标引擎;其 VM 无效或 DebugStatePtr 为空时只记日志并返回
         * @param Port   预留:真实 MobDebug 监听端口(当前未使用)
         *
         * @detail 会把 InstructionCount 归零。注意 hook 是挂在某个具体 VM 上的,
         *   DualVM swap 之后需要重新 Enable。
         */
        static void Enable(MLuaEngine& Engine, uint16 Port = 9339);
        /**
         * @brief Disable - 全局关闭入口。
         *
         * @detail NOTE: 当前是 no-op(只打一条日志)—— 本文件不持有 engine 列表,
         *   无法定位要摘的 hook。实际摘除请用 DisableForEngine(Engine)。
         */
        static void Disable();
    };

    /**
     * @brief DisableForEngine - 关闭单个 engine 的 hook(业务侧拿到 engine 后调用)。
     * @param Engine 目标引擎
     *
     * @detail 置 bRunning=false 并对当前 VM 调 lua_sethook 解绑。DualVM swap 时
     *   用于摘掉旧 VM 的 hook。
     */
    void DisableForEngine(MLuaEngine& Engine);

} // namespace mession::script::lua