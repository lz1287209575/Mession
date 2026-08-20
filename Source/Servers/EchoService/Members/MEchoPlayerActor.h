#pragma once

// F0 ActorMember 框架验证 actor(方案 A):per-player actor 容器。
// 设计:2026-08-14-actor-member-framework.md §3.1 / §4.2(Component 模型)。
//
// actor 自身无业务协议;成员(MPlayerItemContainer 等)由宿主挂载:
//   DemoPlayer->AddMember("MPlayerItemContainer", CreateDefaultSubObject<...>)
// 成员表 = TMemberHostImpl(IMemberHost 默认实现),GetActorMember<T>() 经它查表。
//
// 双继承说明:IActor(actor 消息模型)+ MObject(反射身份/NewMObject 构造),
// 仿 MRankListActor 先例;TMemberHostImpl 提供成员表能力。

#include "Common/Runtime/Actor/ActorMember.h"
#include "Common/Runtime/Actor/IActor.h"

MCLASS(Type = Actor)
class MEchoPlayerActor : public IActor, public MObject, public TMemberHostImpl {
    public:
    MGENERATED_BODY(MEchoPlayerActor, MObject, 0)
    public: // MGENERATED_BODY 展开以 private: 结尾,恢复 public 访问
    explicit MEchoPlayerActor(uint64 InPlayerId) : PlayerId(InPlayerId) {
    }

    // 生成器 SetConstructor<MEchoPlayerActor>() 需要默认构造(反射注册用)。
    MEchoPlayerActor() = default;

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
