/**
 * @file EReloadMode.h
 * @brief EReloadMode —— 脚本热重载策略枚举(经 MENUM 反射注册)。
 *
 * @detail 取值由 MLuaEngine::Reload 派发(见 LuaEngine.cpp):
 *   - AtomicSwap: 原地换 lua_State,脚本侧状态全丢,in-flight 协程被中断;
 *   - DualVM:     双 VM 两阶段 swap,先 drain 再迁 actor state,失败回退 AtomicSwap;
 *   - Discard:    当前未单独实现,Reload 时告警并回退到 AtomicSwap。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

namespace mession::script {

    /**
     * @brief EReloadMode - 热重载模式。
     */
    enum class EReloadMode : uint8 {
        AtomicSwap = 0, // 原地替换 lua_State,丢脚本状态
        DualVM     = 1, // 双 VM 两阶段 swap,保留 actor state
        Discard    = 2, // 预留值:当前回退到 AtomicSwap
    };

} // namespace mession::script

MENUM(EReloadMode)