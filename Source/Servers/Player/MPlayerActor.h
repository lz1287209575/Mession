/**
 * @file MPlayerActor.h
 * @brief MPlayerActor —— 每玩家 actor 容器(见 Docs/superpowers/specs/2026-08-14-player-design.md §3)。
 *
 * @detail actor 自身不带业务协议;业务由挂在它身上的成员(MPlayerItemContainer 等)承载:
 *   Player->AddMember("MPlayerItemContainer", CreateDefaultSubObject<...>(Player.Get(), ...))
 *   成员表由 TMemberHostImpl(IMemberHost 的默认实现)维护,GetActorMember<T>() 经它查表。
 *   GetActorId() == PlayerId,框架成员分发器按请求里的 PlayerId 路由即可命中本 actor。
 *
 * @detail 生命周期:与其它 actor 一样由 TSharedPtr 持有(MActorSystem / MActorRouter 侧),
 *   用 NewMObject<MPlayerActor>(...) 构造,不要裸 new/delete。
 */
#pragma once

#include "Common/Runtime/Actor/ActorMember.h"
#include "Common/Runtime/Actor/IActor.h"

/**
 * @brief MPlayerActor - 单玩家 actor:持有 PlayerId,并作为业务成员的宿主。
 *
 * @detail 反射注册要求可默认构造;正式创建走
 *   NewMObject<MPlayerActor>(nullptr, InName, InPlayerId)(见 MPlayerService::RegisterPlayer)。
 *   成员方法在宿主 actor 的 Sub 线程上执行,成员间共享状态不需要额外加锁。
 */
MCLASS(Type = Actor)
class MPlayerActor : public IActor, public MObject, public TMemberHostImpl {
    public:
    MGENERATED_BODY(MPlayerActor, MObject, 0)
    public: // MGENERATED_BODY 展开以 private: 结尾,恢复 public 访问
    /** @brief 构造 - 指定 PlayerId(同时即 ActorId)。 */
    explicit MPlayerActor(uint64 InPlayerId) : PlayerId(InPlayerId) {
    }

    /** @brief 默认构造 - 供反射注册(生成的 SetConstructor<MPlayerActor>() 需要)。 */
    MPlayerActor() = default;

    /** @brief GetActorId - 返回 PlayerId,IActor 接口实现(成员分发器按 PlayerId 路由命中本 actor)。 */
    uint64 GetActorId() const override {
        return PlayerId;
    }

    /** @brief OnMessage - 空实现:成员分发不走 actor 消息模型(分发器直接调成员方法)。 */
    void OnMessage(const FActorMessage& InMsg) override {
        (void)InMsg;
    }

    /** @brief GetPlayerId - 返回本 actor 承载的玩家 id。 */
    uint64 GetPlayerId() const {
        return PlayerId;
    }

    private:
    // 玩家 id,同时用作 ActorId(两者语义相同,不额外维护映射)。
    uint64 PlayerId = 0;
};
