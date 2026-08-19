/**
 * @file IActor.h
 * @brief 业务 actor 接口 - actor 模型的最小抽象.
 *
 * 设计目标:让业务实体(排行榜 / 公会 / 世界 boss / 拍卖行等)能 actor 化,
 * 获得"单线程访问 actor state"的保证,避免共享锁。
 *
 * 三大契约:
 * 1. GetActorId() - 全局唯一,沿用 MServiceId::Make(ServiceType, InstId)
 * 2. OnMessage(Msg) - 消息处理入口,在 actor 自己的 Sub 线程被调
 * 3. 生命周期回调 - OnCreated() / OnDestroyed()
 *
 * actor 内部 state 由派生类自己持有,外部不可直接访问 —— 唯一访问途径是
 * 通过 MActorHandle::Post / Call 发消息,actor 自己单线程处理。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Script/Abstract/TScriptInstanceHandle.h"

struct FActorMessage;

/**
 * @brief IActor - 业务 actor 基类.
 */
class IActor {
    public:
    virtual ~IActor() = default;

    /**
     * @brief GetActorId - 全局唯一 actor id.
     *
     * 必须用 MServiceId::Make(ServiceType, InstId) 64-bit 布局返回,
     * 与 MActorRouter / MEndpointCache::OnEndpointChange 注册路径一致。
     */
    virtual uint64 GetActorId() const = 0;

    /**
     * @brief OnMessage - 消息处理入口(单线程).
     *
     * 由 MActorSystem 在 actor 自己的 Sub 线程调,actor state 访问无需锁。
     * 业务派生类根据 FActorMessage.Header.MsgType 分发业务逻辑。
     *
     * @param InMsg 消息(只读,Sender/Target/Type/Payload)
     */
    virtual void OnMessage(const FActorMessage& InMsg) = 0;

    /** @brief actor 创建后回调(在自己的 Sub 线程调). */
    virtual void OnCreated() {
    }

    /** @brief actor 销毁前回调(在自己的 Sub 线程调). */
    virtual void OnDestroyed() {
    }

    /**
     * @brief SerializeState - 序列化 actor 内部 state（持久化支持）.
     *
     * 默认实现:返回空 TByteArray（actor 不需要持久化）。
     * 业务派生类重写时,必须**只在 actor 自己的 Sub 线程**被调（直接读 state,无锁）。
     * wire format 由派生类自己定义;MActorSystem 只负责 byte buffer 的存取。
     */
    virtual TByteArray SerializeState() const {
        return TByteArray();
    }

    /**
     * @brief RestoreState - 从 byte buffer 恢复 actor 内部 state.
     *
     * 默认实现:no-op,返回 true（actor 不需要持久化）。
     * 业务派生类重写时,会**在 actor 自己的 Sub 线程**被调（直接写 state,无锁）。
     * 业务应保证:Restore 期间不会收到 OnMessage（MActorSystem::Restore 流程会先
     * Unregister 再 Restore 再 Register,或类似保证）。
     *
     * @return true 成功;false 数据格式不识别,跳过恢复
     */
    virtual bool RestoreState(const TByteArray& InStateBytes) {
        (void)InStateBytes;
        return true;
    }

    /**
     * @brief OnVmSwapped - DualVM 热重载完成后的回调(DualVM Plan).
     *
     * Lua 业务 actor 实现:旧 VM 的 handle 已失效,新 VM 的 handle 是 engine 重新
     * :new + luaL_ref 后给的。默认实现:no-op。Lua-side proxy 派生类 override 时,
     * 把 self.Handle 替换成 NewHandle;如果业务有缓存的 Lua table / closure
     * (例如 setmetatable 之前捕获的对方 actor 句柄),在这里更新。
     *
     * @param OldHandle 旧 VM 的 handle(已失效,只用于比对 / 日志)
     * @param NewHandle 新 VM 的 handle(可立即用于 InvokeInstanceMethod)
     */
    virtual void OnVmSwapped(const mession::script::TScriptInstanceHandle& OldHandle, const mession::script::TScriptInstanceHandle& NewHandle) {
        (void)OldHandle;
        (void)NewHandle;
    }
};