#pragma once

#include "Common/Logger.h"
#include "Common/ServerRpcRuntime.h"

MCLASS()
class MWorldService : public MReflectObject
{
public:
    MGENERATED_BODY(MWorldService, MReflectObject, 0)
    public:

    using FHandler_Rpc_OnPlayerLoginRequest = TFunction<void(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)>;
    using FHandler_Rpc_OnSessionValidateResponse = TFunction<void(uint64 ValidationRequestId, uint64 PlayerId, bool bValid)>;

    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey);

    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnSessionValidateResponse(uint64 ValidationRequestId, uint64 PlayerId, bool bValid);

    static void SetHandler_Rpc_OnPlayerLoginRequest(const FHandler_Rpc_OnPlayerLoginRequest& InHandler);
    static void SetHandler_Rpc_OnSessionValidateResponse(const FHandler_Rpc_OnSessionValidateResponse& InHandler);
    static uint16 GetFunctionId_Rpc_OnPlayerLoginRequest();
    static uint16 GetFunctionId_Rpc_OnSessionValidateResponse();

private:
    inline static FHandler_Rpc_OnPlayerLoginRequest Handler_Rpc_OnPlayerLoginRequest;
    inline static FHandler_Rpc_OnSessionValidateResponse Handler_Rpc_OnSessionValidateResponse;
};

inline void MWorldService::Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)
{
    if (Handler_Rpc_OnPlayerLoginRequest)
    {
        Handler_Rpc_OnPlayerLoginRequest(ClientConnectionId, PlayerId, SessionKey);
        return;
    }

    LOG_WARN("MWorldService Rpc_OnPlayerLoginRequest with no handler bound (ClientConnId=%llu, PlayerId=%llu, SessionKey=%u)",
             static_cast<unsigned long long>(ClientConnectionId),
             static_cast<unsigned long long>(PlayerId),
             static_cast<unsigned>(SessionKey));
}

inline void MWorldService::Rpc_OnSessionValidateResponse(uint64 ValidationRequestId, uint64 PlayerId, bool bValid)
{
    if (Handler_Rpc_OnSessionValidateResponse)
    {
        Handler_Rpc_OnSessionValidateResponse(ValidationRequestId, PlayerId, bValid);
        return;
    }

    LOG_WARN("MWorldService Rpc_OnSessionValidateResponse with no handler bound (ValidationRequestId=%llu, PlayerId=%llu, bValid=%d)",
             static_cast<unsigned long long>(ValidationRequestId),
             static_cast<unsigned long long>(PlayerId),
             bValid ? 1 : 0);
}

inline void MWorldService::SetHandler_Rpc_OnPlayerLoginRequest(const FHandler_Rpc_OnPlayerLoginRequest& InHandler)
{
    Handler_Rpc_OnPlayerLoginRequest = InHandler;
}

inline void MWorldService::SetHandler_Rpc_OnSessionValidateResponse(const FHandler_Rpc_OnSessionValidateResponse& InHandler)
{
    Handler_Rpc_OnSessionValidateResponse = InHandler;
}

inline uint16 MWorldService::GetFunctionId_Rpc_OnPlayerLoginRequest()
{
    return MGET_STABLE_RPC_FUNCTION_ID("MWorldService", "Rpc_OnPlayerLoginRequest");
}

inline uint16 MWorldService::GetFunctionId_Rpc_OnSessionValidateResponse()
{
    return MGET_STABLE_RPC_FUNCTION_ID("MWorldService", "Rpc_OnSessionValidateResponse");
}
