// MemberFrameworkTest — F0 ActorMember 框架单测。
//
// 覆盖(2026-08-14-actor-member-framework.md):
//   1. 继承序列化 round-trip(FPlayerRequestBase ← FPlayerUseItemRequest,
//      PlayerId 基类字段落盘,§5/§6)
//   2. GMemberRpcEntries 查表(FunctionId → (MemberClass, MethodName, RequestType),§3.2)
//   3. 成员挂载 + GetActorMember<T>()(Component 模型,§4.2)
//   4. 框架成员分发 round-trip(DispatchFrameworkMemberCall,§4.3)
//   5. FindActorMember<T>(PlayerId) 显式路径

#include "Common/Net/Rpc/RpcPayload.h"
#include "Common/Runtime/Actor/ActorMember.h"
#include "Common/Runtime/Concurrency/SubReactorPool.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Player/FPlayerMessages.h"
#include "Servers/EchoService/Members/MEchoPlayerActor.h"
#include "Servers/EchoService/Members/MPlayerItemContainer.h"

#include <cassert>
#include <cstdio>

// ---- 1. 继承序列化 round-trip ----
static void TestInheritedRequestRoundTrip() {
    FPlayerUseItemRequest In;
    In.PlayerId = 10001;
    In.ItemId   = 1001;
    In.Count    = 2;

    const TByteArray Bytes = BuildPayload(In);
    assert(!Bytes.empty());

    FPlayerUseItemRequest Out;
    const auto Result = ParsePayload(Bytes, Out);
    assert(Result.IsOk());
    assert(Out.PlayerId == 10001); // 基类字段(继承序列化先父后子)
    assert(Out.ItemId == 1001);
    assert(Out.Count == 2);
    std::printf("ok: TestInheritedRequestRoundTrip (%zu bytes, PlayerId inherited)\n", Bytes.size());
}

// ---- 2. GMemberRpcEntries 查表 ----
static void TestMemberRpcEntryTable() {
    const uint16 FunctionId = ComputeStableReflectId("MPlayerItemContainer", "UseItem");
    const SMemberRpcEntry* Entry = FindMemberRpcEntryByFunctionId(FunctionId);
    assert(Entry != nullptr);
    assert(MString(Entry->MemberClass) == "MPlayerItemContainer");
    assert(MString(Entry->MethodName) == "UseItem");
    assert(MString(Entry->RequestType) == "FPlayerUseItemRequest");
    assert(GetMemberRpcEntryCount() >= 1);
    assert(FindMemberRpcEntry(0) == Entry);
    assert(FindMemberRpcEntryByFunctionId(0xFFFF) == nullptr);
    std::printf("ok: TestMemberRpcEntryTable (id=%u -> %s::%s)\n", static_cast<unsigned>(FunctionId), Entry->MemberClass, Entry->MethodName);
}

// ---- 3. 成员挂载 + GetActorMember ----
static void TestMemberMountAndLookup() {
    TSharedPtr<MEchoPlayerActor> Player = NewMObject<MEchoPlayerActor>(nullptr, "DemoPlayer", 10001);
    Player->AddMember("MPlayerItemContainer", CreateDefaultSubObject<MPlayerItemContainer>(Player.Get(), "ItemContainer"));

    // 宿主侧:IMemberHost::FindMember 按类型名查成员
    MPlayerItemContainer* Container = static_cast<MPlayerItemContainer*>(Player->FindMember("MPlayerItemContainer"));
    assert(Container != nullptr);
    assert(Container->GetHost() == static_cast<IMemberHost*>(Player.Get()));
    assert(Container->GetItemCount(1001) == 5); // OnAttach 预置
    assert(Container->GetItemCount(9999) == 0);

    // 成员侧:IActorMember::GetActorMember<T>() 经宿主成员表查兄弟/自身
    MPlayerItemContainer* ViaMember = Container->GetActorMember<MPlayerItemContainer>();
    assert(ViaMember == Container);

    // 宿主成员表按类型名查询(IMemberHost::FindMember)
    IActorMember* ByName = Player->FindMember("MPlayerItemContainer");
    assert(ByName == static_cast<IActorMember*>(Container));

    // 摘除
    Player->RemoveMember("MPlayerItemContainer");
    assert(Player->FindMember("MPlayerItemContainer") == nullptr);
    assert(Container->GetHost() == nullptr);
    std::printf("ok: TestMemberMountAndLookup (attach/get/remove)\n");
}

// ---- 4. 框架成员分发 round-trip ----
static void TestFrameworkMemberDispatch() {
    // MActorSystem 需要绑定 SubPool(Register 按 Sub 分布 + OnCreated 回调)
    MSubReactorPool SubPool;
    SubPool.Init(1);
    SubPool.Start();
    MActorSystem::Get().Init(&SubPool);

    TSharedPtr<MEchoPlayerActor> Player = NewMObject<MEchoPlayerActor>(nullptr, "DemoPlayer", 10001);
    Player->AddMember("MPlayerItemContainer", CreateDefaultSubObject<MPlayerItemContainer>(Player.Get(), "ItemContainer"));
    MActorSystem::Get().Register(Player);

    const uint16 FunctionId = ComputeStableReflectId("MPlayerItemContainer", "UseItem");

    // 请求:PlayerId 路由到 actor 宿主 → 成员 UseItem(扣 2 个,剩 3)
    FPlayerUseItemRequest Req;
    Req.PlayerId = 10001;
    Req.ItemId   = 1001;
    Req.Count    = 2;

    SFutureResult<TByteArray> RespFuture = DispatchFrameworkMemberCall(FunctionId, BuildPayload(Req));
    assert(RespFuture.IsReady());
    assert(RespFuture.IsOk());

    FPlayerUseItemResponse Resp;
    const auto ParseResult = ParsePayload(RespFuture.Get(), Resp);
    assert(ParseResult.IsOk());
    assert(Resp.bOk);
    assert(Resp.ItemId == 1001);
    assert(Resp.Remaining == 3);

    // 成员状态被真实修改(use 2 of 5)
    MPlayerItemContainer* Container = static_cast<MPlayerItemContainer*>(Player->FindMember("MPlayerItemContainer"));
    assert(Container != nullptr);
    assert(Container->GetItemCount(1001) == 3);

    // 超用:count 超过剩余 → 扣到 0,remaining 0
    Req.Count = 10;
    RespFuture = DispatchFrameworkMemberCall(FunctionId, BuildPayload(Req));
    assert(RespFuture.IsReady() && RespFuture.IsOk());
    ParsePayload(RespFuture.Get(), Resp);
    assert(Resp.bOk);
    assert(Resp.Remaining == 0);

    // 未知 FunctionId → err future
    SFutureResult<TByteArray> Unknown = DispatchFrameworkMemberCall(0xABCD, BuildPayload(Req));
    assert(Unknown.IsReady() && Unknown.IsErr());

    // 不存在的 PlayerId → host not found err
    Req.PlayerId = 99999;
    SFutureResult<TByteArray> NoHost = DispatchFrameworkMemberCall(FunctionId, BuildPayload(Req));
    assert(NoHost.IsReady() && NoHost.IsErr());

    // ---- 5. FindActorMember<T>(PlayerId) 显式路径 ----
    MPlayerItemContainer* Found = FindActorMember<MPlayerItemContainer>(10001);
    assert(Found == Container);
    assert(FindActorMember<MPlayerItemContainer>(99999) == nullptr);

    MActorSystem::Get().Unregister(10001);
    MActorSystem::Get().Shutdown();
    SubPool.Shutdown();
    std::printf("ok: TestFrameworkMemberDispatch (dispatch round-trip + FindActorMember)\n");
}

int main() {
    TestInheritedRequestRoundTrip();
    TestMemberRpcEntryTable();
    TestMemberMountAndLookup();
    TestFrameworkMemberDispatch();
    std::printf("ALL OK\n");
    return 0;
}
