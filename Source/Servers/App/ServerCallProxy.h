#pragma once

#include "Common/Net/Rpc/RpcServerCall.h"
#include "Servers/App/ServerCallAsyncSupport.h"

class MServerCallProxyBase : public MObject
{
public:
    void SetConnection(const TSharedPtr<MServerConnection>& InConnection)
    {
        Connection = InConnection;
    }

    bool IsAvailable() const
    {
        return Connection && Connection->IsConnected() && GetTargetServerType() != EServerType::Unknown;
    }

protected:
    template<typename TResponse, typename TRequest>
    MFuture<TResult<TResponse, FAppError>> CallRemote(const char* FunctionName, const TRequest& Request) const
    {
        const EServerType TargetServerType = GetTargetServerType();
        if (!Connection || !Connection->IsConnected() || TargetServerType == EServerType::Unknown)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                "rpc_target_unavailable",
                FunctionName ? FunctionName : "ServerCall");
        }

        return CallServerFunction<TResponse>(Connection, TargetServerType, FunctionName, Request);
    }

    virtual EServerType GetTargetServerType() const = 0;

    TSharedPtr<MServerConnection> Connection;
};
