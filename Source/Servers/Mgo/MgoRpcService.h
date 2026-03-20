#pragma once

#include "Common/Log/Logger.h"
#include "Common/ServerRpcRuntime.h"

MCLASS()
class MMgoService : public MReflectObject
{
public:
    MGENERATED_BODY(MMgoService, MReflectObject, 0)
    public:

    using FHandler_Rpc_OnPersistSnapshot =
        TFunction<void(uint64 ObjectId, uint16 ClassId, uint32 OwnerWorldId, uint64 RequestId, uint64 Version, const MString& ClassName, const MString& SnapshotHex)>;
    using FHandler_Rpc_OnLoadSnapshotRequest =
        TFunction<void(uint64 RequestId, uint64 ObjectId)>;

    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnPersistSnapshot(uint64 ObjectId, uint16 ClassId, uint32 OwnerWorldId, uint64 RequestId, uint64 Version, const MString& ClassName, const MString& SnapshotHex);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnLoadSnapshotRequest(uint64 RequestId, uint64 ObjectId);

    static void SetHandler_Rpc_OnPersistSnapshot(const FHandler_Rpc_OnPersistSnapshot& InHandler);
    static void SetHandler_Rpc_OnLoadSnapshotRequest(const FHandler_Rpc_OnLoadSnapshotRequest& InHandler);

    template<typename TServer>
    static void BindHandlers(TServer* Server);

private:
    inline static FHandler_Rpc_OnPersistSnapshot Handler_Rpc_OnPersistSnapshot;
    inline static FHandler_Rpc_OnLoadSnapshotRequest Handler_Rpc_OnLoadSnapshotRequest;
};

inline void MMgoService::Rpc_OnPersistSnapshot(uint64 ObjectId, uint16 ClassId, uint32 OwnerWorldId, uint64 RequestId, uint64 Version, const MString& ClassName, const MString& SnapshotHex)
{
    if (Handler_Rpc_OnPersistSnapshot)
    {
        Handler_Rpc_OnPersistSnapshot(ObjectId, ClassId, OwnerWorldId, RequestId, Version, ClassName, SnapshotHex);
        return;
    }

    LOG_WARN("MMgoService Rpc_OnPersistSnapshot with no handler bound (object=%llu, class_id=%u, owner=%u, req=%llu, ver=%llu, class=%s, bytes=%llu)",
             static_cast<unsigned long long>(ObjectId),
             static_cast<unsigned>(ClassId),
             static_cast<unsigned>(OwnerWorldId),
             static_cast<unsigned long long>(RequestId),
             static_cast<unsigned long long>(Version),
             ClassName.c_str(),
             static_cast<unsigned long long>(SnapshotHex.size() / 2));
}

inline void MMgoService::SetHandler_Rpc_OnPersistSnapshot(const FHandler_Rpc_OnPersistSnapshot& InHandler)
{
    Handler_Rpc_OnPersistSnapshot = InHandler;
}

inline void MMgoService::Rpc_OnLoadSnapshotRequest(uint64 RequestId, uint64 ObjectId)
{
    if (Handler_Rpc_OnLoadSnapshotRequest)
    {
        Handler_Rpc_OnLoadSnapshotRequest(RequestId, ObjectId);
        return;
    }

    LOG_WARN("MMgoService Rpc_OnLoadSnapshotRequest with no handler bound (request=%llu, object=%llu)",
             static_cast<unsigned long long>(RequestId),
             static_cast<unsigned long long>(ObjectId));
}

inline void MMgoService::SetHandler_Rpc_OnLoadSnapshotRequest(const FHandler_Rpc_OnLoadSnapshotRequest& InHandler)
{
    Handler_Rpc_OnLoadSnapshotRequest = InHandler;
}

template<typename TServer>
inline void MMgoService::BindHandlers(TServer* Server)
{
    SetHandler_Rpc_OnPersistSnapshot(
        [Server](uint64 ObjectId, uint16 ClassId, uint32 OwnerWorldId, uint64 RequestId, uint64 Version, const MString& ClassName, const MString& SnapshotHex)
        {
            Server->Rpc_OnPersistSnapshot(ObjectId, ClassId, OwnerWorldId, RequestId, Version, ClassName, SnapshotHex);
        });
    SetHandler_Rpc_OnLoadSnapshotRequest(
        [Server](uint64 RequestId, uint64 ObjectId)
        {
            Server->Rpc_OnLoadSnapshotRequest(RequestId, ObjectId);
        });
}
