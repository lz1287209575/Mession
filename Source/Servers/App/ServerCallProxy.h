#pragma once

#include "Common/Net/Rpc/RpcRuntimeContext.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Servers/App/ServerCallAsyncSupport.h"

class MServerCallProxyBase : public MObject
{
public:
    bool IsAvailable() const
    {
        const EServerType TargetServerType = GetTargetServerType();
        if (TargetServerType == EServerType::Unknown)
        {
            return false;
        }

        const IRpcTransportResolver* Resolver = FindTransportResolver();
        return Resolver && Resolver->IsServerTransportAvailable(TargetServerType);
    }

protected:
    template<typename TResponse, typename TRequest>
    MFuture<TResult<TResponse, FAppError>> CallRemoteByName(
        const char* FunctionName,
        const TRequest& Request) const
    {
        const EServerType TargetServerType = GetTargetServerType();
        const TSharedPtr<MServerConnection> Connection = ResolveTargetConnection();
        if (!Connection || !Connection->IsConnected() || TargetServerType == EServerType::Unknown)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                "rpc_target_unavailable",
                FunctionName ? FunctionName : "ServerCall");
        }

        return CallServerFunction<TResponse>(
            Connection,
            TargetServerType,
            FunctionName,
            Request);
    }

    template<typename TResponse, typename TRequest>
    MFuture<TResult<TResponse, FAppError>> CallRemote(
        const MFunction* Function,
        const TRequest& Request,
        const char* MissingFunctionName = nullptr) const
    {
        const EServerType TargetServerType = GetTargetServerType();
        const TSharedPtr<MServerConnection> Connection = ResolveTargetConnection();
        if (!Connection || !Connection->IsConnected() || TargetServerType == EServerType::Unknown)
        {
            const char* FunctionName = (Function && !Function->Name.empty())
                ? Function->Name.c_str()
                : (MissingFunctionName ? MissingFunctionName : "ServerCall");
            return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                "rpc_target_unavailable",
                FunctionName);
        }

        return CallServerFunction<TResponse>(Connection, Function, Request, MissingFunctionName);
    }

    virtual EServerType GetTargetServerType() const = 0;

private:
    const IRpcTransportResolver* FindTransportResolver() const
    {
        for (MObject* Current = GetOuter(); Current; Current = Current->GetOuter())
        {
            if (const auto* RuntimeContext = dynamic_cast<const IServerRuntimeContext*>(Current))
            {
                return RuntimeContext->GetRpcTransportResolver();
            }
        }

        return nullptr;
    }

    TSharedPtr<MServerConnection> ResolveTargetConnection() const
    {
        const IRpcTransportResolver* Resolver = FindTransportResolver();
        return Resolver ? Resolver->ResolveServerTransport(GetTargetServerType()) : nullptr;
    }
};
