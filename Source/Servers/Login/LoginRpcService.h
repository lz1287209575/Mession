#pragma once

#include "Common/Logger.h"
#include "Common/ServerRpcRuntime.h"

MCLASS()
class MLoginService : public MReflectObject
{
public:
    MGENERATED_BODY(MLoginService, MReflectObject, 0)
    public:

    using FHandler_Rpc_OnPlayerLoginRequest = TFunction<void(uint64 ClientConnectionId, uint64 PlayerId)>;
    using FHandler_Rpc_OnSessionValidateRequest = TFunction<void(uint64 ValidationRequestId, uint64 PlayerId, uint32 SessionKey)>;

    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId);

    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnSessionValidateRequest(uint64 ValidationRequestId, uint64 PlayerId, uint32 SessionKey);

    static void SetHandler_Rpc_OnPlayerLoginRequest(const FHandler_Rpc_OnPlayerLoginRequest& InHandler);
    static void SetHandler_Rpc_OnSessionValidateRequest(const FHandler_Rpc_OnSessionValidateRequest& InHandler);
    static uint16 GetFunctionId_Rpc_OnPlayerLoginRequest();
    static uint16 GetFunctionId_Rpc_OnSessionValidateRequest();

private:
    inline static FHandler_Rpc_OnPlayerLoginRequest Handler_Rpc_OnPlayerLoginRequest;
    inline static FHandler_Rpc_OnSessionValidateRequest Handler_Rpc_OnSessionValidateRequest;
};

inline void MLoginService::Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId)
{
    if (Handler_Rpc_OnPlayerLoginRequest)
    {
        Handler_Rpc_OnPlayerLoginRequest(ClientConnectionId, PlayerId);
        return;
    }

    LOG_WARN("MLoginService Rpc_OnPlayerLoginRequest with no handler bound (ClientConnId=%llu, PlayerId=%llu)",
             static_cast<unsigned long long>(ClientConnectionId),
             static_cast<unsigned long long>(PlayerId));
}

inline void MLoginService::Rpc_OnSessionValidateRequest(uint64 ValidationRequestId, uint64 PlayerId, uint32 SessionKey)
{
    if (Handler_Rpc_OnSessionValidateRequest)
    {
        Handler_Rpc_OnSessionValidateRequest(ValidationRequestId, PlayerId, SessionKey);
        return;
    }

    LOG_WARN("MLoginService Rpc_OnSessionValidateRequest with no handler bound (ValidationRequestId=%llu, PlayerId=%llu, SessionKey=%u)",
             static_cast<unsigned long long>(ValidationRequestId),
             static_cast<unsigned long long>(PlayerId),
             static_cast<unsigned>(SessionKey));
}

inline void MLoginService::SetHandler_Rpc_OnPlayerLoginRequest(const FHandler_Rpc_OnPlayerLoginRequest& InHandler)
{
    Handler_Rpc_OnPlayerLoginRequest = InHandler;
}

inline void MLoginService::SetHandler_Rpc_OnSessionValidateRequest(const FHandler_Rpc_OnSessionValidateRequest& InHandler)
{
    Handler_Rpc_OnSessionValidateRequest = InHandler;
}

inline uint16 MLoginService::GetFunctionId_Rpc_OnPlayerLoginRequest()
{
    return MGET_STABLE_RPC_FUNCTION_ID("MLoginService", "Rpc_OnPlayerLoginRequest");
}

inline uint16 MLoginService::GetFunctionId_Rpc_OnSessionValidateRequest()
{
    return MGET_STABLE_RPC_FUNCTION_ID("MLoginService", "Rpc_OnSessionValidateRequest");
}
