/**
 * @file MRpcChannel.cpp
 * @brief MRpcChannel 的实现:actor 三种投递语义与 ServerCall 转发。
 */

#include "Common/Net/Rpc/MRpcChannel.h"

#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Runtime/Actor/FActorMessage.h"
#include "Common/Runtime/Actor/MActorSystem.h"
#include "Common/Runtime/Object/Result.h"

MRpcChannel& MRpcChannel::Get() {
    static MRpcChannel Instance;
    return Instance;
}

SFutureResult<TByteArray> MRpcChannel::CallActor(uint64 InActorId, const FActorMessage& InMsg) const {
    // 1) 组装 wire 信封:把进程内 FActorMessage 拆成可序列化字段。
    //    ReplyPromise 不上 wire(只对 caller 进程有意义),跨进程回包走标准
    //    ServerCall 响应通道。
    FActorMessageWire Envelope;
    Envelope.SenderId = InMsg.Header.SenderId;
    Envelope.TargetId = InMsg.Header.TargetId;
    Envelope.MsgType  = InMsg.Header.MsgType;
    Envelope.Payload  = InMsg.Payload;

    // 2) 寻址:查 MActorRouter 的路由表。ServerType == Unknown 表示本进程本地 actor,
    //    远端 ServerType 则路由到对端 endpoint;查不到路由时按 Unknown 走(由下游报错)。
    const SActorRoute Route   = MActorRouter::Get().FindActor(InActorId);
    const EServerType ServerT = Route.ActorId ? Route.ServerType : EServerType::Unknown;

    // 3) 走通用 ServerCall 通道,反射到对端的 MEchoService::OnActorMessage,
    //    对端再 MActorSystem::DispatchLocal(ActorId, Msg)。Post 语义:不等待处理完成。
    return Call<TByteArray>(ServerT, "MEchoService", "OnActorMessage", Envelope);
}

bool MRpcChannel::SendActor(uint64 InActorId, const FActorMessage& InMsg) const {
    // 真·fire-and-forget:用 MT_ActorPost 包(无 CallId,对端不回任何包)。
    // 相比早期用 MT_FunctionCall 的版本:
    // - 消除 wasted RTT(对端不再回一个空的 FunctionResponse)
    // - 省掉 client 侧 ConsumeServerCall 的无效查找
    // - 省一次空包的网络带宽与对端 CPU
    //
    // 返回 false 的三种原因:路由表里没有该 actor / 连接未就绪 / SendBuffer 满。
    // 调用方(MActorHandle::Post)应把消息放进 per-actor outbox,等 OnEndpointChange
    // 触发 drain 重发。

    // 1) 组装 wire 信封(与 CallActor 一致,复用同一结构)。
    FActorMessageWire Envelope;
    Envelope.SenderId = InMsg.Header.SenderId;
    Envelope.TargetId = InMsg.Header.TargetId;
    Envelope.MsgType  = InMsg.Header.MsgType;
    Envelope.Payload  = InMsg.Payload;
    // SequenceId 由调用方(MActorSystem::SendActor)用 AllocateSequenceId 分配,本函数只透传。
    // 它用于与对端的 MT_ServerPush ack 匹配,从而删除 outbox 条目。
    Envelope.SequenceId = InMsg.SequenceId;

    // 2) 寻址:查 MActorRouter 路由表,ServerType 决定本地还是跨进程投递。
    const SActorRoute Route = MActorRouter::Get().FindActor(InActorId);
    if (Route.ActorId == 0) {
        LOG_DEBUG("SendActor: actor %llu not in route table", static_cast<unsigned long long>(InActorId));
        return false; // 容错:未注册 actor
    }
    auto Connection = MEndpointCache::Get().GetOrConnect(Route.ServerType);
    if (!Connection || !Connection->IsConnected()) {
        LOG_DEBUG("SendActor: connection_unavailable type=%s", GetServerTypeDisplayName(Route.ServerType));
        return false; // 容错:连不上
    }

    // 3) 找 OnActorMessage 的 FunctionId
    const MClass* TargetClass = MObject::FindClass("MEchoService");
    if (!TargetClass)
        return false;
    const MFunction* Function = TargetClass->FindFunction("OnActorMessage");
    if (!Function)
        return false;

    // 4) 构造 MT_ActorPost 包:[FunctionId:2B][Payload:N],无 CallId、无长度前缀。
    //    FunctionId 按小端手写(与对端 DispatchActorPostMessage 的解析一致)。
    const TByteArray EnvelopeBytes = BuildPayload(Envelope);
    TByteArray       Packet;
    Packet.reserve(2 + EnvelopeBytes.size());
    const uint16 FunctionId = Function->FunctionId;
    Packet.push_back(static_cast<uint8>(FunctionId & 0xFF));
    Packet.push_back(static_cast<uint8>((FunctionId >> 8) & 0xFF));
    Packet.insert(Packet.end(), EnvelopeBytes.begin(), EnvelopeBytes.end());

    // 5) 裸 send:Connection 的发送缓冲非阻塞,业务线程立即返回(不代表对端已处理)
    if (!Connection->SendPacket(static_cast<uint8>(EServerMessageType::MT_ActorPost), Packet.data(), static_cast<uint32>(Packet.size()))) {
        LOG_DEBUG("SendActor: Send failed for actor=%llu", static_cast<unsigned long long>(InActorId));
        return false;
    }
    return true;
}

SFutureResult<TByteArray> MRpcChannel::CallActorAndWait(uint64 InActorId, const FActorMessage& InMsg) const {
    // Call 语义:走对端 OnActorCall(返回 FActorMessageWire,Payload = actor 的响应字节)。
    // ReplyPromise 同样不上 wire —— 由对端 OnActorCall 自己构造,串到 actor 一侧。
    FActorMessageWire Request;
    Request.SenderId = InMsg.Header.SenderId;
    Request.TargetId = InMsg.Header.TargetId;
    Request.MsgType  = InMsg.Header.MsgType;
    Request.Payload  = InMsg.Payload;

    const SActorRoute Route   = MActorRouter::Get().FindActor(InActorId);
    const EServerType ServerT = Route.ActorId ? Route.ServerType : EServerType::Unknown;

    // 用 CallServerFunction<FActorMessageWire> 等对端 OnActorCall 返回,把 Payload 字节交给
    // caller。Then 回调里做类型桥接:err 原样透传,ok 时取出 envelope.Payload。
    auto Promise = MakeShared<MPromise<TResult<TByteArray, FAppError>>>();
    auto Future  = SFutureResult<TByteArray>(Promise->GetFuture());

    Call<FActorMessageWire>(ServerT, "MEchoService", "OnActorCall", Request).Then([Promise](MFuture<TResult<FActorMessageWire, FAppError>> F) mutable {
        TResult<FActorMessageWire, FAppError> R = F.Get();
        if (R.IsErr()) {
            Promise->SetValue(TResult<TByteArray, FAppError>::Err(R.GetError()));
            return;
        }
        Promise->SetValue(TResult<TByteArray, FAppError>::Ok(R.GetValue().Payload));
    });

    return Future;
}
