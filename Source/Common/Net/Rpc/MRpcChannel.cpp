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
    // 1) wire envelope —— 把进程内 FActorMessage 拆成可序列化字段。
    //    ReplyPromise 不在 wire 上(只对 caller 进程有意义),跨进程回包走
    //    标准 ServerCall 响应通道(后续 Call 语义 TODO)。
    FActorMessageWire Envelope;
    Envelope.SenderId = InMsg.Header.SenderId;
    Envelope.TargetId = InMsg.Header.TargetId;
    Envelope.MsgType  = InMsg.Header.MsgType;
    Envelope.Payload  = InMsg.Payload;

    // 2) 寻址:MActorRouter 已有 SActorRoute,本进程路由表给出 ServerType=Unknown
    //    走本进程 path;远端 ServerType 走对端 endpoint。空表 → Unknown。
    const SActorRoute Route   = MActorRouter::Get().FindActor(InActorId);
    const EServerType ServerT = Route.ActorId ? Route.ServerType : EServerType::Unknown;

    // 3) 走通用 ServerCall 通道 —— 反射到对端 MEchoService::OnActorMessage,
    //    对端再 MActorSystem::DispatchLocal(ActorId, Msg)。Post 语义,不等待回包。
    return Call<TByteArray>(ServerT, "MEchoService", "OnActorMessage", Envelope);
}

bool MRpcChannel::SendActor(uint64 InActorId, const FActorMessage& InMsg) const {
    // 真·fire-and-forget:用 MT_ActorPost 包（无 CallId,server 不回任何包）。
    // 相比 SendActor 之前的 MT_FunctionCall 版本:
    // - 消除 wasted RTT（server 不发空 FunctionResponse）
    // - 消除 client ConsumeServerCall not found 的查找
    // - 节省一次空包的网络带宽 + server CPU
    //
    // 返回 false 时:路由无 / 连接未就绪 / SendBuffer 满。
    // 调用方（MActorHandle::Post）应进 per-actor outbox,等 OnEndpointChange 触发 drain。

    // 1) wire envelope
    FActorMessageWire Envelope;
    Envelope.SenderId = InMsg.Header.SenderId;
    Envelope.TargetId = InMsg.Header.TargetId;
    Envelope.MsgType  = InMsg.Header.MsgType;
    Envelope.Payload  = InMsg.Payload;
    // 阶段 C 完善:SequenceId 由调用方(MActorSystem::SendActor)分配,这里透传。
    // SendActor 也负责把 SequenceId 写到 InMsg.SequenceId 用于 outbox ack 匹配。
    Envelope.SequenceId = InMsg.SequenceId;

    // 2) 寻址
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

    // 4) 构造 MT_ActorPost 包 —— [FunctionId:2B][Payload:N] 无 CallId
    const TByteArray EnvelopeBytes = BuildPayload(Envelope);
    TByteArray       Packet;
    Packet.reserve(2 + EnvelopeBytes.size());
    const uint16 FunctionId = Function->FunctionId;
    Packet.push_back(static_cast<uint8>(FunctionId & 0xFF));
    Packet.push_back(static_cast<uint8>((FunctionId >> 8) & 0xFF));
    Packet.insert(Packet.end(), EnvelopeBytes.begin(), EnvelopeBytes.end());

    // 5) 裸 send —— Connection.SendBuffer 非阻塞,业务线程立刻返回
    if (!Connection->SendPacket(static_cast<uint8>(EServerMessageType::MT_ActorPost), Packet.data(), static_cast<uint32>(Packet.size()))) {
        LOG_DEBUG("SendActor: Send failed for actor=%llu", static_cast<unsigned long long>(InActorId));
        return false;
    }
    return true;
}

SFutureResult<TByteArray> MRpcChannel::CallActorAndWait(uint64 InActorId, const FActorMessage& InMsg) const {
    // Call 语义:走 OnActorCall(返回 FActorMessageWire,Payload = actor 响应字节)。
    // ReplyPromise 也不上 wire —— 对端 OnActorCall 自己构造 ReplyPromise 串到 actor 端。
    FActorMessageWire Request;
    Request.SenderId = InMsg.Header.SenderId;
    Request.TargetId = InMsg.Header.TargetId;
    Request.MsgType  = InMsg.Header.MsgType;
    Request.Payload  = InMsg.Payload;

    const SActorRoute Route   = MActorRouter::Get().FindActor(InActorId);
    const EServerType ServerT = Route.ActorId ? Route.ServerType : EServerType::Unknown;

    // CallServerFunction<FActorMessageWire> 等对端 OnActorCall 返回,提取 Payload 字节给 caller。
    // Then 回调里 bridge 到 TByteArray future:err 透传,ok 把 envelope.Payload 解出来。
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
