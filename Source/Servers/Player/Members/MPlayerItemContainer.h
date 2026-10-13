/**
 * @file MPlayerItemContainer.h
 * @brief MPlayerItemContainer —— 玩家背包成员(ActorMember 业务协议示例)。
 *
 * @detail 设计见 Docs/superpowers/specs/2026-08-14-actor-member-framework.md §1 / §4.1。
 *   协议下沉到成员:Service 类(MPlayerService)零业务协议,客户端请求经
 *   Gateway -> MPlayerService::FrameworkMemberDispatch(FunctionId, Payload) ->
 *   框架成员分发器(DispatchFrameworkMemberCall) -> 本成员的 UseItem。
 *
 * @detail 线程约定:成员方法在宿主 actor 的 Sub 线程上执行,因此 Items 无需加锁;
 *   但这也意味着不要从其它线程直接调本类方法。
 */
#pragma once

#include "Common/Net/Rpc/RpcPayload.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Runtime/Actor/ActorMember.h"
#include "Protocol/Messages/Player/FPlayerMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

/**
 * @brief MPlayerItemContainer - 玩家背包成员:物品数量表 + UseItem 业务协议。
 *
 * @detail 作为 IActorMember 挂在 MPlayerActor 上,随宿主 actor 一起创建/销毁;
 *   状态自持(Items),不落在进程级容器里。
 */
MCLASS(Type = ActorMember)
class MPlayerItemContainer : public IActorMember {
    public:
    MGENERATED_BODY(MPlayerItemContainer, IActorMember, 0)
    public: // MGENERATED_BODY 展开以 private: 结尾,恢复 public 访问
    /**
     * @brief UseItem - 背包成员 ServerCall:使用物品并扣减数量。
     * @param InRequest.ItemId 物品 id
     * @param InRequest.Count  请求使用数量;实际扣减 min(Count, 当前剩余)
     * @return 响应 future:bOk = 是否成功,Remaining = 扣减后的剩余数量
     *
     * @detail 物品不存在或数量 <= 0 不算"错误",返回 bOk=false 且 Remaining=0(不抛错,
     *   调用方据 bOk 判断)。在宿主 actor 的 Sub 线程执行。
     */
    MFUNCTION(ServerCall)
    SFutureResult<FPlayerUseItemResponse> UseItem(const FPlayerUseItemRequest& InRequest);

    /** @brief OnAttach - IActorMember 钩子:挂载时预置演示背包。
     *  (OnAttach 由 AddMember -> AttachToHost 触发,见 ActorMember.cpp) */
    void OnAttach() override;

    /** @brief SetItemCount - 测试辅助:直接设置某物品数量(同线程调用,不校验非负)。 */
    void SetItemCount(uint64 InItemId, int32 InCount);

    /** @brief GetItemCount - 测试辅助:读某物品数量;不存在时返回 0。 */
    int32 GetItemCount(uint64 InItemId) const;

    private:
    // ItemId → 数量(成员自持状态,生命周期跟随宿主 actor)。
    TMap<uint64, int32> Items;
};
