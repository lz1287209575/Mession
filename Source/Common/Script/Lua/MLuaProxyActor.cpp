/**
 * @file MLuaProxyActor.cpp
 * @brief 实现 MLuaProxyActor:把 actor 生命周期与消息转发到 Lua 钩子。
 */
#include "Common/Script/Lua/MLuaProxyActor.h"
#include "Common/Runtime/Actor/IActor.h"
#include "Common/Script/Abstract/ScriptErrorCodes.h"
#include "Common/Script/Lua/LuaEngine.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    /**
     * @brief FActorMessageLite - FActorMessage 的轻量镜像,用于避免引入 actor.h 全头。
     *
     * @detail NOTE: 当前仅作占位 —— OnMessage 并未真正解包 Payload(序列化待实做)。
     */
    struct FActorMessageLite {
        uint64_t    Sender;
        uint64_t    Target;
        uint32_t    MsgType;
        const void* Payload;
        size_t      PayloadLen;
    };

    namespace {
        /**
         * @brief PushActorSelf - 经 registry ref 把该 actor 的 Lua 实例压到栈顶。
         * @param L      目标 Lua 状态机
         * @param Handle 目标实例句柄(其 Id 即 registry ref)
         * @return 压栈值非 nil 时为 true;为 nil 时返回 false(该值仍留在栈上,由调用方 pop)
         */
        bool PushActorSelf(lua_State* L, TScriptInstanceHandle Handle) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, Handle.GetId());
            return !lua_isnil(L, -1);
        }

        /**
         * @brief GetInstanceField - 取栈顶对象的字段;字段为 nil 时返回 false。
         * @param L     目标 Lua 状态机
         * @param Field 字段名
         * @return 字段非 nil 为 true,否则 false
         *
         * @detail 无论结果如何都会压入一个值(字段值或 nil)。
         */
        bool GetInstanceField(lua_State* L, const char* Field) {
            lua_getfield(L, -1, Field);
            return !lua_isnil(L, -1);
        }
    } // namespace

    /** @brief 构造 - 记录所属 engine / 反射类 / actor id / instance handle / 类名 */
    MLuaProxyActor::MLuaProxyActor(MLuaEngine& InEngine, MClass* InCls, uint64 InActorId, TScriptInstanceHandle InHandle, MString InClassName)
        : Engine(InEngine), Cls(InCls), ActorId(InActorId), Handle(InHandle), ClassName(std::move(InClassName)) {
    }

    /** @brief 析构 - 默认实现,不主动释放 Lua 侧实例 */
    MLuaProxyActor::~MLuaProxyActor() = default;

    /**
     * @brief OnMessage - 调用 Lua 端 on_message 钩子。
     * @param InMsg 消息体;NOTE 当前未使用(payload 序列化待实做)
     *
     * @detail generation 不符、取不到 self、或没有 on_message 时静默返回;
     *   钩子抛错时仅 pop 掉错误值,不向上传播。
     */
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

    /** @brief OnCreated - 调用 Lua 端 on_created 钩子(generation 不符或钩子缺失时直接返回) */
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

    /** @brief OnDestroyed - 调用 Lua 端 on_destroyed 钩子(generation 不符或钩子缺失时直接返回) */
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

    /**
     * @brief OnVmSwapped - 用新 handle 覆写自身缓存的 handle。
     * @param OldHandle 旧 handle;NOTE 当前未使用
     * @param NewHandle 新 handle
     */
    void MLuaProxyActor::OnVmSwapped(const TScriptInstanceHandle& /*OldHandle*/, const TScriptInstanceHandle& NewHandle) {
        // ReplaceState 已经把 Handle 改成了新 handle(rebind 时已经在 LuaEngine 侧替换)
        // 这里只用于业务层缓存的句柄更新
        Handle = NewHandle;
    }

    /** @brief SaveLuaState - 调 Lua 端 __dualvm_save();无该钩子时返回 Ok(空串) */
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
                Err           = P ? MString(P) : MString("dualvm_save_failed");
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
        MString     S(P, Len);
        lua_pop(L, 1);
        return TResult<MString>::Ok(S);
    }

    /** @brief RestoreLuaState - 调 Lua 端 __dualvm_restore(Snapshot);无该钩子时直接 Ok() */
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
                Err           = P ? MString(P) : MString("dualvm_restore_failed");
            } else {
                Err = MString("dualvm_restore_failed");
            }
            lua_pop(L, 1);
            return TResult<void>::Err(Err);
        }
        return TResult<void>::Ok();
    }

    /** @brief RebindHandleOnCurrentVm - 在当前 VM 上以空实参重建实例并替换 Handle */
    void MLuaProxyActor::RebindHandleOnCurrentVm() {
        // 在当前 active VM 上重新 :new(args) — args 暂时传空(后续 plan 支持 ctor args 缓存)
        MString ClassName = this->ClassName;
        auto    R         = Engine.CreateInstanceByClassName(ClassName, mession::script::TScriptArgs(nullptr, 0));
        if (R.IsOk()) {
            Handle = R.GetValue();
        }
        // 失败时保留旧 handle(generation 已 stale,后续 invoke 会 fail-fast)
    }

} // namespace mession::script::lua