/**
 * @file LuaHotReload.h
 * @brief MLuaHotReload —— MLuaEngine 的两种热重载算法(纯静态函数,无状态)。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Abstract/EReloadResult.h"

namespace mession::script::lua {

    class MLuaEngine;

    /**
     * @brief MLuaHotReload - 热重载算法集合。
     *
     * @detail 两个入口都直接改 Engine 的 VM 状态,不在函数内部加锁
     *   (锁由 MLuaEngine::BeginSwap / EndSwap 自己取)。
     *   前置条件:Engine 已 Initialize,且由外部保证没有其它线程并发 Reload。
     */
    class MLuaHotReload {
        public:
        /**
         * @brief ReloadDualVM - 双 VM 两阶段热重载:先 drain,drain 失败则回退 AtomicSwap。
         * @param Engine   目标引擎
         * @param NewBytes 新脚本源码
         * @return Ok(Success);新建 VM / load 失败时 Err(不改变当前活跃 VM)
         *
         * @detail 顺序:drain(30s 上限)→ 快照 actor state → 建 VM_B + 装 stdlib →
         *   load → BeginSwap(++generation)→ 恢复 actor → 重绑跨实例引用 → EndSwap。
         *   任何失败发生在 BeginSwap 之前,旧 VM 仍可用、generation 不变。
         */
        static TResult<EReloadResult> ReloadDualVM(MLuaEngine& Engine, const MString& NewBytes);

        /**
         * @brief ReloadAtomicSwap - 简化热重载:不 drain、不迁移状态直接换 VM。
         * @param Engine   目标引擎
         * @param NewBytes 新脚本源码
         * @return Ok(Success);新建 VM / load 失败时 Err
         *
         * @detail 脚本侧状态与 in-flight 协程全部丢弃。
         */
        static TResult<EReloadResult> ReloadAtomicSwap(MLuaEngine& Engine, const MString& NewBytes);
    };

} // namespace mession::script::lua