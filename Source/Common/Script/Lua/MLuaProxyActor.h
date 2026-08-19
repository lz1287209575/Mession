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

    // MLuaProxyActor — 包装一个 Lua 侧 actor 实例
    // 持有 TScriptInstanceHandle(VM registry ref + generation)
    // OnMessage 把消息体 invoke Lua 端方法;OnCreated/Destroyed 调 Lua 钩子
    // OnVmSwapped 在 DualVM swap 后替换 handle(由 MLuaEngine::RebindCrossInstanceRefs 调)
    class MLuaProxyActor : public IActor {
        public:
        MLuaProxyActor(MLuaEngine& InEngine, MClass* InCls, uint64 InActorId,
                       TScriptInstanceHandle InHandle, MString InClassName);
        ~MLuaProxyActor() override;

        // IActor
        uint64 GetActorId() const override {
            return ActorId;
        }
        void   OnMessage(const struct FActorMessage& InMsg) override;
        void   OnCreated() override;
        void   OnDestroyed() override;
        void   OnVmSwapped(const mession::script::TScriptInstanceHandle& OldHandle,
                            const mession::script::TScriptInstanceHandle& NewHandle) override;

        // Lua state 序列化(供 SaveAllActorStates 调用)
        // 优先调 Lua 端 __dualvm_save() 钩子;无钩子走 C++ 反射 fallback
        TResult<MString> SaveLuaState() const;

        // Lua state 反序列化(供 RestoreAllActorStates 调用)
        // 优先调 Lua 端 __dualvm_restore(state) 钩子;无钩子走 C++ 反射 fallback
        TResult<void> RestoreLuaState(const MString& Snapshot);

        // 在当前 active VM 上重新创建 Lua 实例(供 VM swap 后重建)
        void RebindHandleOnCurrentVm();

        // accessors
        MClass*                GetClass() const {
            return Cls;
        }
        const MString&         GetClassName() const {
            return ClassName;
        }
        TScriptInstanceHandle  GetHandle() const {
            return Handle;
        }
        void                   SetHandle(TScriptInstanceHandle H) {
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