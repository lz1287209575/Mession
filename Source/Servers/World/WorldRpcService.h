#pragma once

#include "Common/Runtime/Log/Logger.h"
#include "Common/Net/ServerRpcRuntime.h"

MCLASS()
class MWorldService : public MObject
{
public:
    MGENERATED_BODY(MWorldService, MObject, 0)
    public:

    using FHandler_Rpc_OnPlayerLoginRequest = TFunction<void(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)>;
    using FHandler_Rpc_OnSessionValidateResponse = TFunction<void(uint64 ValidationRequestId, uint64 PlayerId, bool bValid)>;
    using FHandler_Rpc_OnMgoLoadSnapshotResponse =
        TFunction<void(uint64 RequestId, uint64 ObjectId, bool bFound, uint16 ClassId, const MString& ClassName, const MString& SnapshotHex)>;
    using FHandler_Rpc_OnMgoPersistSnapshotResult =
        TFunction<void(uint32 OwnerWorldId, uint64 RequestId, uint64 ObjectId, uint64 Version, bool bSuccess, const MString& Reason)>;

    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey);

    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnSessionValidateResponse(uint64 ValidationRequestId, uint64 PlayerId, bool bValid);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnMgoLoadSnapshotResponse(uint64 RequestId, uint64 ObjectId, bool bFound, uint16 ClassId, const MString& ClassName, const MString& SnapshotHex);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnMgoPersistSnapshotResult(uint32 OwnerWorldId, uint64 RequestId, uint64 ObjectId, uint64 Version, bool bSuccess, const MString& Reason);

    static void SetHandler_Rpc_OnPlayerLoginRequest(const FHandler_Rpc_OnPlayerLoginRequest& InHandler);
    static void SetHandler_Rpc_OnSessionValidateResponse(const FHandler_Rpc_OnSessionValidateResponse& InHandler);
    static void SetHandler_Rpc_OnMgoLoadSnapshotResponse(const FHandler_Rpc_OnMgoLoadSnapshotResponse& InHandler);
    static void SetHandler_Rpc_OnMgoPersistSnapshotResult(const FHandler_Rpc_OnMgoPersistSnapshotResult& InHandler);
    template<typename TServer>
    static void BindHandlers(TServer* Server);

private:
    inline static FHandler_Rpc_OnPlayerLoginRequest Handler_Rpc_OnPlayerLoginRequest;
    inline static FHandler_Rpc_OnSessionValidateResponse Handler_Rpc_OnSessionValidateResponse;
    inline static FHandler_Rpc_OnMgoLoadSnapshotResponse Handler_Rpc_OnMgoLoadSnapshotResponse;
    inline static FHandler_Rpc_OnMgoPersistSnapshotResult Handler_Rpc_OnMgoPersistSnapshotResult;
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

inline void MWorldService::Rpc_OnMgoLoadSnapshotResponse(uint64 RequestId, uint64 ObjectId, bool bFound, uint16 ClassId, const MString& ClassName, const MString& SnapshotHex)
{
    if (Handler_Rpc_OnMgoLoadSnapshotResponse)
    {
        Handler_Rpc_OnMgoLoadSnapshotResponse(RequestId, ObjectId, bFound, ClassId, ClassName, SnapshotHex);
        return;
    }

    LOG_WARN("MWorldService Rpc_OnMgoLoadSnapshotResponse with no handler bound (request=%llu, object=%llu, found=%d)",
             static_cast<unsigned long long>(RequestId),
             static_cast<unsigned long long>(ObjectId),
             bFound ? 1 : 0);
}

inline void MWorldService::SetHandler_Rpc_OnMgoLoadSnapshotResponse(const FHandler_Rpc_OnMgoLoadSnapshotResponse& InHandler)
{
    Handler_Rpc_OnMgoLoadSnapshotResponse = InHandler;
}

inline void MWorldService::Rpc_OnMgoPersistSnapshotResult(
    uint32 OwnerWorldId,
    uint64 RequestId,
    uint64 ObjectId,
    uint64 Version,
    bool bSuccess,
    const MString& Reason)
{
    if (Handler_Rpc_OnMgoPersistSnapshotResult)
    {
        Handler_Rpc_OnMgoPersistSnapshotResult(OwnerWorldId, RequestId, ObjectId, Version, bSuccess, Reason);
        return;
    }

    LOG_WARN("MWorldService Rpc_OnMgoPersistSnapshotResult with no handler bound (owner=%u request=%llu object=%llu version=%llu success=%d reason=%s)",
             static_cast<unsigned>(OwnerWorldId),
             static_cast<unsigned long long>(RequestId),
             static_cast<unsigned long long>(ObjectId),
             static_cast<unsigned long long>(Version),
             bSuccess ? 1 : 0,
             Reason.c_str());
}

inline void MWorldService::SetHandler_Rpc_OnMgoPersistSnapshotResult(const FHandler_Rpc_OnMgoPersistSnapshotResult& InHandler)
{
    Handler_Rpc_OnMgoPersistSnapshotResult = InHandler;
}

template<typename TServer>
inline void MWorldService::BindHandlers(TServer* Server)
{
    SetHandler_Rpc_OnPlayerLoginRequest(
        [Server](uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)
        {
            Server->Rpc_OnPlayerLoginRequest(ClientConnectionId, PlayerId, SessionKey);
        });
    SetHandler_Rpc_OnSessionValidateResponse(
        [Server](uint64 ValidationRequestId, uint64 PlayerId, bool bValid)
        {
            Server->Rpc_OnSessionValidateResponse(ValidationRequestId, PlayerId, bValid);
        });
    SetHandler_Rpc_OnMgoLoadSnapshotResponse(
        [Server](uint64 RequestId, uint64 ObjectId, bool bFound, uint16 ClassId, const MString& ClassName, const MString& SnapshotHex)
        {
            Server->Rpc_OnMgoLoadSnapshotResponse(RequestId, ObjectId, bFound, ClassId, ClassName, SnapshotHex);
        });
    SetHandler_Rpc_OnMgoPersistSnapshotResult(
        [Server](uint32 OwnerWorldId, uint64 RequestId, uint64 ObjectId, uint64 Version, bool bSuccess, const MString& Reason)
        {
            Server->Rpc_OnMgoPersistSnapshotResult(OwnerWorldId, RequestId, ObjectId, Version, bSuccess, Reason);
        });
}
