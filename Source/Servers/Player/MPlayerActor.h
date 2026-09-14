#pragma once

// MPlayerActor — 每玩家 actor 容器(2026-08-14 player-design §3)。
//
// actor 自身无业务协议;成员(MPlayerItemContainer 等)由宿主挂载:
//   Player->AddMember("MPlayerItemContainer", CreateDefaultSubObject<...>)
// 成员表 = TMemberHostImpl(IMemberHost 默认实现),GetActorMember<T>() 经它查表。
// GetActorId = PlayerId:框架成员分发器按请求里的 PlayerId 路由命中本 actor。

#include "Common/Runtime/Actor/ActorMember.h"
#include "Common/Runtime/Actor/IActor.h"

MCLASS(Type = Actor)
class MPlayerActor : public IActor, public MObject, public TMemberHostImpl {
    public:
    MGENERATED_BODY(MPlayerActor, MObject, 0)
    public: // MGENERATED_BODY 展开以 private: 结尾,恢复 public 访问
    explicit MPlayerActor(uint64 InPlayerId) : PlayerId(InPlayerId) {
    }

    // 生成器 SetConstructor<MPlayerActor>() 需要默认构造(反射注册用)。
    MPlayerActor() = default;

    // IActor 接口:GetActorId = PlayerId(成员分发器按 PlayerId 路由命中本 actor)
    uint64 GetActorId() const override {
        return PlayerId;
    }

    // 成员分发不走 actor 消息模型(分发器直接调成员方法),OnMessage 留空。
    void OnMessage(const FActorMessage& InMsg) override {
        (void)InMsg;
    }

    uint64 GetPlayerId() const {
        return PlayerId;
    }

    private:
    uint64 PlayerId = 0;
};
