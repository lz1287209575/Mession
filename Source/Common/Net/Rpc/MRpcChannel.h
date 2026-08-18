#pragma once

#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/Rpc/RpcClientCall.h"
#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Net/ServiceDiscovery/EndpointCache.h"
#include "Common/Runtime/Actor/FActorMessage.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Log/Log.h"

#include <utility>

/**
 * MRpcChannel - 统一 RPC 调用通道
 *
 * 统一 ServerCall 和 CallClient，提供一致的异步调用体验。
 *
 * Server-side transport resolution（选 connection / lazy connect）由
 * MEndpointCache 全局单例负责——Caller 不用再传 IRpcTransportResolver。
 *
 * 用法(spec 2026-07-24 §18 附录 A):
 *
 * // ServerCall: 调用远程服务器,得到 ready/pending SFutureResult<FResp>
 * auto fut = MRpcChannel::Get().Call<FResponse>(
 *     EServerType::Echo, "MEchoService", "Echo", request);
 * fut.Then([](SFutureResult<FResponse> F)
 * {
 *     if (F.IsOk()) { // use F.GetResult().GetValue()
 *     }
 *     else          { // handle F.GetError()
 *     }
 * });
 *
 * // CallClient: 发送到客户端
 * MRpcChannel::Get().SendToClient(connection, "MPlayerController", "OnNotify", response);
 */
class MRpcChannel
{
public:
    static MRpcChannel& Get();

    /**
     * @brief CallActor - 跨进程 actor 消息投递(actor-extension 阶段 4 / multi-reactor 阶段 6).
     *
     * 把 FActorMessage 序列化进 FActorMessageWire,经 MRpcChannel::Call 投递到目标进程
     * 的 MEchoService::OnActorMessage ServerCall;接收端反射到
     * MActorSystem::DispatchLocal(ActorId, Msg)。
     *
     * 寻址:从 MActorRouter 查 ActorId 所在 ServerType;空路由时回退到 EServerType::Unknown。
     *
     * Post 语义:fire-and-forget(SFutureResult<TByteArray> 仍返回,但调用方一般丢弃)。
     *
     * @param InActorId 目标 actor id(全局 64-bit)
     * @param InMsg     actor 消息
     * @return         Future —— 通常 await 后丢弃(Post 语义无业务回包)
     */
    SFutureResult<TByteArray> CallActor(uint64 InActorId, const FActorMessage& InMsg) const;

    /**
     * @brief SendActor - 真·fire-and-forget Post（不产生 MPromise / 不注册 callback）.
     *
     * 与 CallActor 的关键区别：
     * - 不分配 MPromise（没有 future 就没有 promise）
     * - 不调 RegisterServerCall（map 里没条目 → server 回的 FunctionResponse 被 silently 丢）
     * - 不等任何东西（Send 写 SendBuffer 即返回,业务线程立刻走）
     *
     * wire 用 MT_ActorPost 包（无 CallId,server 不回任何包 → 0 RTT wasted）。
     *
     * @return true  消息已成功 enqueue 到对端连接（业务线程调用后即返回,不阻塞）
     * @return false 失败（路由无 / 连接未就绪 / SendBuffer 满 / 进程退出中）;
     *                调用方应进 per-actor outbox,等 OnEndpointChange 触发 drain 重发
     */
    bool SendActor(uint64 InActorId, const FActorMessage& InMsg) const;

    /**
     * @brief CallActorAndWait - 跨进程 actor Call(请求-响应),Post 不适用。
     *
     * 与 CallActor 区别:
     * - CallActor:走 OnActorMessage ServerCall(SEmptyServerMessage 回包),
     *              fire-and-forget(Post 语义)。
     * - CallActorAndWait:走 OnActorCall ServerCall(FActorMessageWire 回包,
     *              Payload = actor 响应字节),等待对端 actor OnMessage 处理完
     *              并通过 Msg.ReplyPromise SetValue 后才 resolve。
     *
     * 用法:业务侧 `MActorHandle::Call(Msg)` 内部用本方法把回复字节接回
     * caller 进程的 ReplyPromise,完成跨进程 actor 调用的请求-响应链路。
     *
     * @param InActorId 目标 actor id
     * @param InMsg     actor 消息(ReplyPromise 字段不上 wire,对端会自己构造)
     * @return         Future,resolve 时 .GetResult().GetValue() 是 actor 响应字节
     */
    SFutureResult<TByteArray> CallActorAndWait(uint64 InActorId, const FActorMessage& InMsg) const;

    // ============================================
    // ServerCall: 调用远程服务器
    // ============================================

    template<typename TResponse, typename TRequest>
    SFutureResult<TResponse> Call(
        EServerType TargetServer,
        const char* ClassName,
        const char* MethodName,
        const TRequest& Request) const
    {
        TSharedPtr<MServerConnection> Connection = MEndpointCache::Get().GetOrConnect(TargetServer);
        if (!Connection || !Connection->IsConnected())
        {
            return MakeRpcErrorFuture<TResponse>("connection_unavailable", TargetServer == EServerType::Unknown ? "server_type_invalid" : "");
        }

        const MClass* TargetClass = MObject::FindClass(ClassName);
        if (!TargetClass)
        {
            return MakeRpcErrorFuture<TResponse>("class_not_found", ClassName);
        }

        return CallServerFunction<TResponse>(Connection, TargetClass, MethodName, Request);
    }

    // 带 Actor 路由
    template<typename TResponse, typename TRequest>
    SFutureResult<TResponse> CallToActor(
        uint64 ActorId,
        const char* ClassName,
        const char* MethodName,
        const TRequest& Request,
        EServerType DefaultServer = EServerType::Unknown) const
    {
        return MActorRouter::Get().SendToActor<TResponse>(
            ActorId, ClassName, MethodName, Request, DefaultServer);
    }

    // ============================================
    // CallClient: 发送到客户端
    // ============================================

    template<typename TResponse, typename TConnection>
    bool SendToClient(
        const TConnection& Connection,
        const char* ClassName,
        const char* MethodName,
        const TResponse& Response) const
    {
        const MClass* TargetClass = MObject::FindClass(ClassName);
        if (!TargetClass)
        {
            LOG_ERROR("MRpcChannel::SendToClient - class not found: %s", ClassName);
            return false;
        }

        const MFunction* Function = TargetClass->FindFunction(MethodName);
        if (!Function)
        {
            LOG_ERROR("MRpcChannel::SendToClient - function not found: %s::%s", ClassName, MethodName);
            return false;
        }

        return BuildAndSendClientRpcMessage(Connection, Function->FunctionId, Response);
    }
};