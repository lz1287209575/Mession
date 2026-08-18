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
};