/**
 * @file MLuaProxyActor.h
 * @brief MLuaProxyActor —— 把 Lua 侧 actor 实例适配为 C++ IActor 的声明。
 */
#pragma once

#include "Common/Runtime/Actor/IActor.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Abstract/TScriptInstanceHandle.h"

#include <cstdint>

extern "C" {
struct lua_State;
}

class MClass;

namespace mession::script::lua {

    class MLuaEngine;
    struct FActorMessageLite; // 见 MLuaProxyActor.cpp

    /**
     * @brief MLuaProxyActor - 把一个 Lua 侧 actor 实例适配为 C++ 的 IActor。
     *
     * @detail 持有 TScriptInstanceHandle(VM registry ref + generation)。
     *   OnMessage 调用 Lua 端 on_message;OnCreated / OnDestroyed 分别调
     *   on_created / on_destroyed 钩子;OnVmSwapped 在 DualVM swap 后替换 handle
     *   (由 MLuaEngine::RebindCrossInstanceRefs 调用)。
     *   NOTE: 除 OnVmSwapped 外,各钩子入口都先经 Handle.Matches(VmGeneration)
     *   做 generation 校验,不匹配则直接静默丢弃。
     */
    class MLuaProxyActor : public IActor {
        public:
        MLuaProxyActor(MLuaEngine& InEngine, MClass* InCls, uint64 InActorId, TScriptInstanceHandle InHandle, MString InClassName);
        ~MLuaProxyActor() override;

        // IActor
        /** @brief GetActorId - 返回创建时登记的 actor id */
        uint64 GetActorId() const override {
            return ActorId;
        }
        /** @brief OnMessage - 调用 Lua 端 on_message 钩子(payload 序列化尚未实做) */
        void OnMessage(const struct FActorMessage& InMsg) override;
        /** @brief OnCreated - 调用 Lua 端 on_created 钩子 */
        void OnCreated() override;
        /** @brief OnDestroyed - 调用 Lua 端 on_destroyed 钩子 */
        void OnDestroyed() override;
        /**
         * @brief OnVmSwapped - VM swap 后用新 handle 覆写自身缓存。
         * @param OldHandle 旧 handle;NOTE 当前实现未使用
         * @param NewHandle 新 handle
         */
        void OnVmSwapped(const mession::script::TScriptInstanceHandle& OldHandle, const mession::script::TScriptInstanceHandle& NewHandle) override;

        /**
         * @brief SaveLuaState - 调 Lua 端 __dualvm_save() 序列化该 actor 的 Lua 状态。
         * @return 钩子返回值;无该钩子时 Ok(空串),出错或 handle 失效时 Err
         *
         * @detail NOTE: 注释所述的 "无钩子走 C++ 反射 fallback" 并未实现。
         */
        TResult<MString> SaveLuaState() const;

        /**
         * @brief RestoreLuaState - 调 Lua 端 __dualvm_restore(snapshot) 反序列化。
         * @param Snapshot 之前 SaveLuaState 产生的快照串
         * @return 无该钩子时直接 Ok(),出错或 handle 失效时 Err
         *
         * @detail NOTE: 注释所述的 "无钩子走 C++ 反射 fallback" 并未实现。
         */
        TResult<void> RestoreLuaState(const MString& Snapshot);

        /**
         * @brief RebindHandleOnCurrentVm - 在当前 active VM 上重新 new 实例并替换 Handle。
         *
         * @detail NOTE: 以空实参调 CreateInstanceByClassName(ctor args 缓存尚未实现);
         *   失败时保留旧 handle(其 generation 已 stale,后续 invoke 会 fail-fast)。
         */
        void RebindHandleOnCurrentVm();

        // 访问器
        /** @brief GetClass - 取该实例的反射 MClass */
        MClass* GetClass() const {
            return Cls;
        }
        /** @brief GetClassName - 取实例类名 */
        const MString& GetClassName() const {
            return ClassName;
        }
        /** @brief GetHandle - 取当前 instance handle */
        TScriptInstanceHandle GetHandle() const {
            return Handle;
        }
        /** @brief SetHandle - 覆写当前 instance handle */
        void SetHandle(TScriptInstanceHandle H) {
            Handle = H;
        }

        private:
        MLuaEngine&           Engine;
        MClass*               Cls;
        uint64                ActorId;
        TScriptInstanceHandle Handle;
        MString               ClassName;
    };

} // namespace mession::script::lua