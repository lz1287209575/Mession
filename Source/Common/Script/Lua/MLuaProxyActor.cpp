#include "Common/Script/Lua/MLuaProxyActor.h"
#include "Common/Runtime/Actor/IActor.h"
#include "Common/Script/Abstract/ScriptErrorCodes.h"
#include "Common/Script/Lua/LuaEngine.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    // FActorMessage 的轻量声明,避免引入 actor.h 全头
    // 实际消息体 payload 走 InvokeInstanceMethod 时不再展开(后续 task 实做 payload 序列化)
    struct FActorMessageLite {
        uint64_t    Sender;
        uint64_t    Target;
        uint32_t    MsgType;
        const void* Payload;
        size_t      PayloadLen;
    };

    namespace {
        // 把 actor proxy 实例 push 到 Lua 栈顶:先 registry ref 拿 Lua instance,再 push self 引用给 Lua
        // 用 Lua 5.4 lua_pushvalue 即可
        bool PushActorSelf(lua_State* L, TScriptInstanceHandle Handle) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, Handle.GetId());
            return !lua_isnil(L, -1);
        }

        // 查找 instance 上的方法 field;若 nil 返回 false
        bool GetInstanceField(lua_State* L, const char* Field) {
            lua_getfield(L, -1, Field);
            return !lua_isnil(L, -1);
        }
    }

    MLuaProxyActor::MLuaProxyActor(MLuaEngine& InEngine, MClass* InCls, uint64 InActorId,
                                   TScriptInstanceHandle InHandle, MString InClassName)
        : Engine(InEngine), Cls(InCls), ActorId(InActorId), Handle(InHandle),
          ClassName(std::move(InClassName)) {
    }

    MLuaProxyActor::~MLuaProxyActor() = default;

    void MLuaProxyActor::OnMessage(const FActorMessage& /*InMsg*/) {
        // PoC:invoke Lua 端 on_message 方法(payload 序列化后续 task 实做)
        lua_State* L = Engine.GetStateForReload().GetLuaState();
        if (!L) {
            return;
        }
        // 检查 generation:跨 VM 的 stale handle 拒绝 invoke
        if (!Handle.Matches(Engine.GetVmGeneration())) {
            return; // silently drop — caller 期望 post-swap 不再投递
        }
        if (!PushActorSelf(L, Handle)) {
            lua_pop(L, 1);
            return;
        }
        if (GetInstanceField(L, "on_message")) {
            if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                // 错误留在栈顶,pop + log
                lua_pop(L, 1);
            }
        } else {
            // 没有 on_message — pop method,pop self
            lua_pop(L, 2);
        }
    }

    void MLuaProxyActor::OnCreated() {
        lua_State* L = Engine.GetStateForReload().GetLuaState();
        if (!L || !Handle.Matches(Engine.GetVmGeneration())) {
            return;
        }
        if (!PushActorSelf(L, Handle)) {
            lua_pop(L, 1);
            return;
        }
        if (GetInstanceField(L, "on_created")) {
            lua_pcall(L, 0, 0, 0);
        } else {
            lua_pop(L, 2);
        }
    }

    void MLuaProxyActor::OnDestroyed() {
        lua_State* L = Engine.GetStateForReload().GetLuaState();
        if (!L || !Handle.Matches(Engine.GetVmGeneration())) {
            return;
        }
        if (!PushActorSelf(L, Handle)) {
            lua_pop(L, 1);
            return;
        }
        if (GetInstanceField(L, "on_destroyed")) {
            lua_pcall(L, 0, 0, 0);
        } else {
            lua_pop(L, 2);
        }
    }

    void MLuaProxyActor::OnVmSwapped(const TScriptInstanceHandle& /*OldHandle*/,
                                    const TScriptInstanceHandle& NewHandle) {
        // ReplaceState 已经把 Handle 改成了新 handle(rebind 时已经在 LuaEngine 侧替换)
        // 这里只用于业务层缓存的句柄更新
        Handle = NewHandle;
    }

    TResult<MString> MLuaProxyActor::SaveLuaState() const {
        lua_State* L = Engine.GetStateForReload().GetLuaState();
        if (!L || !Handle.Matches(Engine.GetVmGeneration())) {
            return TResult<MString>::Err(MString(ScriptErrorCodes::kVmSwapped));
        }
        if (!PushActorSelf(L, Handle)) {
            lua_pop(L, 1);
            return TResult<MString>::Err(MString("instance_nil"));
        }
        if (!GetInstanceField(L, "__dualvm_save")) {
            lua_pop(L, 2);
            return TResult<MString>::Ok(MString()); // 无钩子,空 snapshot
        }
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
            // 错误:pop error,return Err
            MString Err;
            if (lua_isstring(L, -1)) {
                const char* P = lua_tostring(L, -1);
                Err            = P ? MString(P) : MString("dualvm_save_failed");
            } else {
                Err = MString("dualvm_save_failed");
            }
            lua_pop(L, 1);
            return TResult<MString>::Err(Err);
        }
        if (!lua_isstring(L, -1)) {
            lua_pop(L, 1);
            return TResult<MString>::Err(MString("dualvm_save_must_return_string"));
        }
        size_t      Len = 0;
        const char* P   = lua_tolstring(L, -1, &Len);
        MString S(P, Len);
        lua_pop(L, 1);
        return TResult<MString>::Ok(S);
    }

    TResult<void> MLuaProxyActor::RestoreLuaState(const MString& Snapshot) {
        lua_State* L = Engine.GetStateForReload().GetLuaState();
        if (!L || !Handle.Matches(Engine.GetVmGeneration())) {
            return TResult<void>::Err(MString(ScriptErrorCodes::kVmSwapped));
        }
        if (!PushActorSelf(L, Handle)) {
            lua_pop(L, 1);
            return TResult<void>::Err(MString("instance_nil"));
        }
        if (!GetInstanceField(L, "__dualvm_restore")) {
            lua_pop(L, 2);
            return TResult<void>::Ok(); // 无钩子,no-op
        }
        lua_pushlstring(L, Snapshot.c_str(), Snapshot.size());
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            MString Err;
            if (lua_isstring(L, -1)) {
                const char* P = lua_tostring(L, -1);
                Err            = P ? MString(P) : MString("dualvm_restore_failed");
            } else {
                Err = MString("dualvm_restore_failed");
            }
            lua_pop(L, 1);
            return TResult<void>::Err(Err);
        }
        return TResult<void>::Ok();
    }

    void MLuaProxyActor::RebindHandleOnCurrentVm() {
        // 在当前 active VM 上重新 :new(args) — args 暂时传空(后续 plan 支持 ctor args 缓存)
        MString ClassName = this->ClassName;
        auto R = Engine.CreateInstanceByClassName(ClassName, mession::script::TScriptArgs(nullptr, 0));
        if (R.IsOk()) {
            Handle = R.GetValue();
        }
        // 失败时保留旧 handle(generation 已 stale,后续 invoke 会 fail-fast)
    }

} // namespace mession::script::lua