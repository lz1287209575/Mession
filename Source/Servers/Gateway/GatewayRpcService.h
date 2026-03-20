#pragma once

#include "Common/Runtime/Log/Logger.h"
#include "Common/Net/ServerRpcRuntime.h"

MCLASS()
class MGatewayService : public MObject
{
public:
    MGENERATED_BODY(MGatewayService, MObject, 0)
    public:

    using FHandler_Rpc_OnPlayerLoginResponse = TFunction<void(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)>;

    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnPlayerLoginResponse(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey);

    static void SetHandler_Rpc_OnPlayerLoginResponse(const FHandler_Rpc_OnPlayerLoginResponse& InHandler);
    template<typename TServer>
    static void BindHandlers(TServer* Server);

private:
    inline static FHandler_Rpc_OnPlayerLoginResponse Handler_Rpc_OnPlayerLoginResponse;
};

inline void MGatewayService::Rpc_OnPlayerLoginResponse(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)
{
    if (Handler_Rpc_OnPlayerLoginResponse)
    {
        Handler_Rpc_OnPlayerLoginResponse(ClientConnectionId, PlayerId, SessionKey);
        return;
    }

    LOG_WARN("MGatewayService Rpc_OnPlayerLoginResponse with no handler bound (ClientConnId=%llu, PlayerId=%llu, SessionKey=%u)",
             static_cast<unsigned long long>(ClientConnectionId),
             static_cast<unsigned long long>(PlayerId),
             static_cast<unsigned>(SessionKey));
}

inline void MGatewayService::SetHandler_Rpc_OnPlayerLoginResponse(const FHandler_Rpc_OnPlayerLoginResponse& InHandler)
{
    Handler_Rpc_OnPlayerLoginResponse = InHandler;
}

template<typename TServer>
inline void MGatewayService::BindHandlers(TServer* Server)
{
    SetHandler_Rpc_OnPlayerLoginResponse(
        [Server](uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)
        {
            Server->Rpc_OnPlayerLoginResponse(ClientConnectionId, PlayerId, SessionKey);
        });
}
