#pragma once

#include "Common/Runtime/MLib.h"

#include <atomic>

namespace mession::script::lua {

    class MLuaEngine;

    // FMobDebugState — per-engine 的 MobDebug hook 状态(DualVM 兼容)
    // 之前是文件-static,会让双 MLuaEngine 共存时互相覆盖;
    // 改为 MLuaEngine 持有实例,DualVM swap 时旧 VM 的状态不再生效(hook 已 lua_sethook 解绑)
    struct FMobDebugState {
        std::atomic<bool>   bRunning{false};
        std::atomic<uint32> InstructionCount{0};
        std::atomic<uint64> LastHookTimestamp{0}; // ms since epoch
    };

    class MLuaMobDebugServer {
        public:
        static void Enable(MLuaEngine& Engine, uint16 Port = 9339);
        static void Disable();
    };

    // 单 engine disable(MobDebugServer.cpp 内部)
    // DualVM swap 时调用 — 摘旧 VM 的 hook,标记 bRunning=false
    void DisableForEngine(MLuaEngine& Engine);

} // namespace mession::script::lua