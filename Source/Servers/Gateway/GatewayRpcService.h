#pragma once

#include "Common/Logger.h"
#include "Common/ServerRpcRuntime.h"

MCLASS()
class MGatewayService : public MReflectObject
{
public:
    MGENERATED_BODY(MGatewayService, MReflectObject, 0)
    public:

    using FHandler_Rpc_OnPlayerLoginResponse = TFunction<void(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)>;

    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnPlayerLoginResponse(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey);

    static void SetHandler_Rpc_OnPlayerLoginResponse(const FHandler_Rpc_OnPlayerLoginResponse& InHandler);
    static uint16 GetFunctionId_Rpc_OnPlayerLoginResponse();

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

inline uint16 MGatewayService::GetFunctionId_Rpc_OnPlayerLoginResponse()
{
    return MGET_STABLE_RPC_FUNCTION_ID("MGatewayService", "Rpc_OnPlayerLoginResponse");
}
