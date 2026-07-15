#pragma once

#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/Rpc/RpcClientCall.h"
#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Net/ServiceDiscovery/EndpointCache.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Log/Logger.h"

#include <utility>

/**
 * MRpcChannel - 统一 RPC 调用通道
 *
 * 统一 ServerCall 和 ClientCall，提供一致的异步调用体验。
 *
 * Server-side transport resolution（选 connection / lazy connect）由
 * MEndpointCache 全局单例负责——Caller 不用再传 IRpcTransportResolver。
 *
 * 用法:
 *
 * // ServerCall: 调用远程服务器
 * auto response = MRpcChannel::Get().Call<FResponse>(
 *     EServerType::Echo, "MEchoService", "Echo", request);
 * FResponse result = MAwaitOk(response);
 *
 * // ClientCall: 发送到客户端
 * MRpcChannel::Get().SendToClient(connection, "MPlayerController", "OnNotify", response);
 */
class MRpcChannel
{
public:
    static MRpcChannel& Get();

    // ============================================
    // ServerCall: 调用远程服务器
    // ============================================

    template<typename TResponse, typename TRequest>
    MFUTURE(TResponse) Call(
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
    MFUTURE(TResponse) CallToActor(
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
    // ClientCall: 发送到客户端
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