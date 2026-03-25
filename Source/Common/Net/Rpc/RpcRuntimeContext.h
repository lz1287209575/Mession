#pragma once

#include "Common/Net/ServerConnection.h"

class IRpcTransportResolver
{
public:
    virtual ~IRpcTransportResolver() = default;
    virtual TSharedPtr<MServerConnection> ResolveServerTransport(EServerType TargetServerType) const = 0;

    virtual bool IsServerTransportAvailable(EServerType TargetServerType) const
    {
        const TSharedPtr<MServerConnection> Connection = ResolveServerTransport(TargetServerType);
        return Connection && Connection->IsConnected();
    }
};

class IServerRuntimeContext
{
public:
    virtual ~IServerRuntimeContext() = default;
    virtual const IRpcTransportResolver* GetRpcTransportResolver() const = 0;
};

class MServerRuntimeContext : public IServerRuntimeContext, public IRpcTransportResolver
{
public:
    const IRpcTransportResolver* GetRpcTransportResolver() const override
    {
        return this;
    }

    TSharedPtr<MServerConnection> ResolveServerTransport(EServerType TargetServerType) const override
    {
        auto It = RpcTransports.find(TargetServerType);
        return (It != RpcTransports.end()) ? It->second : nullptr;
    }

    void RegisterRpcTransport(EServerType TargetServerType, const TSharedPtr<MServerConnection>& Connection)
    {
        if (TargetServerType == EServerType::Unknown)
        {
            return;
        }

        if (Connection)
        {
            RpcTransports[TargetServerType] = Connection;
        }
        else
        {
            RpcTransports.erase(TargetServerType);
        }
    }

    void UnregisterRpcTransport(EServerType TargetServerType)
    {
        RpcTransports.erase(TargetServerType);
    }

    void ClearRpcTransports()
    {
        RpcTransports.clear();
    }

private:
    TMap<EServerType, TSharedPtr<MServerConnection>> RpcTransports;
};
