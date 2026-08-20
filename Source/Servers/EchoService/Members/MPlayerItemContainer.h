#pragma once

// F0 ActorMember 框架验证成员(方案 A:在 EchoService 拓扑上验证)。
// 设计:2026-08-14-actor-member-framework.md §1 / §4.1。
//
// 协议下沉到成员:Service 类(EchoService)不再声明背包业务协议,客户端经
// Gateway → EchoService.FrameworkMemberDispatch(FunctionId, Payload) →
// 框架成员分发器(DispatchFrameworkMemberCall)→ 本成员 UseItem。

#include "Common/Net/Rpc/RpcPayload.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Runtime/Actor/ActorMember.h"
#include "Protocol/Messages/Player/FPlayerMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

MCLASS(Type = ActorMember)
class MPlayerItemContainer : public IActorMember {
    public:
    MGENERATED_BODY(MPlayerItemContainer, IActorMember, 0)
    public: // MGENERATED_BODY 展开以 private: 结尾,恢复 public 访问
    /** 背包成员 ServerCall:使用物品(数量扣减)。 */
    MFUNCTION(ServerCall)
    SFutureResult<FPlayerUseItemResponse> UseItem(const FPlayerUseItemRequest& InRequest);

    // IActorMember 钩子:挂载时预置演示背包(OnAttach 在 AddMember →
    // AttachToHost 时被调,见 ActorMember.cpp)。
    void OnAttach() override;

    // 测试辅助:背包状态读写(同线程;PoC 成员方法在调用线程执行)。
    void  SetItemCount(uint64 InItemId, int32 InCount);
    int32 GetItemCount(uint64 InItemId) const;

    private:
    // ItemId → 数量(成员自持状态,生命周期跟随宿主 actor)。
    TMap<uint64, int32> Items;
};
