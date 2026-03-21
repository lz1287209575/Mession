#include "WorldServer.h"
#include "Common/Net/ServerRpcRuntime.h"
#include "Common/Runtime/HexUtils.h"
#include "Common/Runtime/Time.h"
#include "Servers/World/Avatar/PlayerAvatar.h"

void MWorldServer::Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)
{
    const uint64 GatewayBackendConnectionId = FindAuthenticatedBackendConnectionId(EServerType::Gateway);
    if (GatewayBackendConnectionId == 0)
    {
        LOG_WARN("No authenticated Gateway backend available for player login request (ClientConnId=%llu, PlayerId=%llu)",
                 (unsigned long long)ClientConnectionId,
                 (unsigned long long)PlayerId);
        return;
    }

    RequestSessionValidation(GatewayBackendConnectionId, PlayerId, SessionKey);
}

void MWorldServer::Rpc_OnSessionValidateResponse(uint64 ValidationRequestId, uint64 PlayerId, bool bValid)
{
    OnLogin_SessionValidateResponse(ValidationRequestId, PlayerId, bValid);
}

void MWorldServer::Rpc_OnMgoLoadSnapshotResponse(
    uint64 RequestId,
    uint64 ObjectId,
    bool bFound,
    uint16 ClassId,
    const MString& ClassName,
    const MString& SnapshotHex)
{
    auto PendingIt = PendingMgoLoads.find(RequestId);
    if (PendingIt == PendingMgoLoads.end())
    {
        return;
    }

    const SPendingMgoLoad Pending = PendingIt->second;
    PendingMgoLoads.erase(PendingIt);

    if (Pending.PlayerId != ObjectId)
    {
        LOG_WARN("Mgo load response mismatch: request=%llu expected_player=%llu got_object=%llu",
                 static_cast<unsigned long long>(RequestId),
                 static_cast<unsigned long long>(Pending.PlayerId),
                 static_cast<unsigned long long>(ObjectId));
        return;
    }

    CompletePlayerLogin(
        Pending.PlayerId,
        Pending.GatewayConnectionId,
        Pending.SessionKey,
        bFound,
        ClassId,
        ClassName,
        SnapshotHex);

    if (bFound)
    {
        LOG_DEBUG("Loaded player state before enter world: request=%llu player=%llu class=%s",
                  static_cast<unsigned long long>(RequestId),
                  static_cast<unsigned long long>(Pending.PlayerId),
                  ClassName.c_str());
    }
}

void MWorldServer::Rpc_OnMgoPersistSnapshotResult(
    uint32 OwnerWorldId,
    uint64 RequestId,
    uint64 ObjectId,
    uint64 Version,
    bool bSuccess,
    const MString& Reason)
{
    if (OwnerWorldId != Config.OwnerServerId)
    {
        return;
    }

    auto It = PendingMgoPersists.find(RequestId);
    if (It == PendingMgoPersists.end())
    {
        ++PersistAckUnmatchedCount;
        return;
    }

    PendingMgoPersists.erase(It);
    if (bSuccess)
    {
        ++PersistAckSuccessCount;
        return;
    }

    ++PersistAckFailedCount;
    LOG_WARN("Mgo persist ACK failed: request=%llu object=%llu version=%llu reason=%s",
             static_cast<unsigned long long>(RequestId),
             static_cast<unsigned long long>(ObjectId),
             static_cast<unsigned long long>(Version),
             Reason.c_str());
}

void MWorldServer::OnPersistRequestDispatched(uint64 RequestId, uint64 ObjectId, uint64 Version)
{
    PendingMgoPersists[RequestId] = SPendingMgoPersist{
        RequestId,
        ObjectId,
        Version,
        MTime::GetTimeSeconds(),
    };
}

bool MWorldServer::RequestMgoLoad(uint64 PlayerId, uint64 GatewayConnectionId, uint32 SessionKey)
{
    if (!MgoServerConn || !MgoServerConn->IsConnected())
    {
        return false;
    }

    uint64 RequestId = NextMgoLoadRequestId++;
    if (RequestId == 0)
    {
        RequestId = NextMgoLoadRequestId++;
    }

    PendingMgoLoads[RequestId] = SPendingMgoLoad{
        RequestId,
        GatewayConnectionId,
        PlayerId,
        SessionKey,
    };

    const bool bSent = MRpc::Call(
        MgoServerConn,
        EServerType::Mgo,
        "Rpc_OnLoadSnapshotRequest",
        RequestId,
        PlayerId);
    if (!bSent)
    {
        PendingMgoLoads.erase(RequestId);
        LOG_WARN("World->Mgo load request send failed: request=%llu player=%llu",
                 static_cast<unsigned long long>(RequestId),
                 static_cast<unsigned long long>(PlayerId));
        return false;
    }

    LOG_DEBUG("World requested Mgo load: request=%llu player=%llu",
              static_cast<unsigned long long>(RequestId),
              static_cast<unsigned long long>(PlayerId));
    return true;
}

void MWorldServer::BeginLoadPlayerState(uint64 PlayerId, uint64 GatewayConnectionId, uint32 SessionKey)
{
    if (Config.EnableMgoPersistence && MgoServerConn && MgoServerConn->IsConnected())
    {
        if (RequestMgoLoad(PlayerId, GatewayConnectionId, SessionKey))
        {
            return;
        }
    }

    CompletePlayerLogin(
        PlayerId,
        GatewayConnectionId,
        SessionKey,
        false,
        0,
        "",
        "");
}

void MWorldServer::CompletePlayerLogin(
    uint64 PlayerId,
    uint64 GatewayConnectionId,
    uint32 SessionKey,
    bool bApplyLoadedSnapshot,
    uint16 LoadedClassId,
    const MString& LoadedClassName,
    const MString& LoadedSnapshotHex)
{
    MPlayerAvatar* Avatar = CreateRuntimePlayer(
        PlayerId,
        GatewayConnectionId,
        SessionKey);
    if (!Avatar)
    {
        return;
    }

    if (bApplyLoadedSnapshot &&
        !ApplyLoadedSnapshotToPlayer(Avatar, LoadedClassId, LoadedClassName, LoadedSnapshotHex))
    {
        LOG_WARN("Apply loaded snapshot failed for player %llu", static_cast<unsigned long long>(PlayerId));
    }

    EnterWorld(Avatar);
    (void)SendInventoryPullToPlayer(PlayerId);
}

bool MWorldServer::ApplyLoadedSnapshotToPlayer(MPlayerAvatar* PlayerAvatar, uint16 ClassId, const MString& ClassName, const MString& SnapshotHex)
{
    if (!PlayerAvatar)
    {
        return false;
    }

    MPlayerSession* Player = PlayerAvatar->GetPlayerSession();
    if (!Player)
    {
        return false;
    }

    TByteArray SnapshotBytes;
    if (!Hex::TryDecodeHex(SnapshotHex, SnapshotBytes))
    {
        LOG_WARN("Loaded snapshot decode failed: player=%llu invalid_hex", static_cast<unsigned long long>(Player->PlayerId));
        return false;
    }

    MClass* ClassMeta = PlayerAvatar->GetClass();
    if (!ClassMeta)
    {
        return false;
    }

    if (!ClassName.empty() && ClassName != ClassMeta->GetName())
    {
        LOG_WARN("Loaded snapshot class mismatch: player=%llu avatar=%s snapshot=%s class_id=%u",
                 static_cast<unsigned long long>(Player->PlayerId),
                 ClassMeta->GetName().c_str(),
                 ClassName.c_str(),
                 static_cast<unsigned>(ClassId));
        return false;
    }

    ClassMeta->ReadSnapshotByDomain(
        PlayerAvatar,
        SnapshotBytes,
        ToMask(EPropertyDomainFlags::Persistence));
    return true;
}

void MWorldServer::SendRouterRegister()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    if (!MRpc::CallRemote(
            RouterServerConn,
            "MRouterServer",
            "Rpc_OnPeerServerRegister",
            Config.OwnerServerId,
            static_cast<uint8>(EServerType::World),
            Config.ServerName,
            MString("127.0.0.1"),
            Config.ListenPort,
            Config.ZoneId))
    {
        LOG_WARN("World->Router register RPC send failed");
    }
}

void MWorldServer::QueryLoginServerRoute()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    const uint64 RequestId = NextRouteRequestId++;
    if (!MRpc::CallRemote(
            RouterServerConn,
            "MRouterServer",
            "Rpc_OnPeerRouteQuery",
            Config.OwnerServerId,
            RequestId,
            static_cast<uint8>(EServerType::Login),
            static_cast<uint64>(0),
            static_cast<uint16>(0)))
    {
        LOG_WARN("World->Router login route query RPC send failed (request=%llu)",
                 static_cast<unsigned long long>(RequestId));
    }
}

void MWorldServer::QueryMgoServerRoute()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    const uint64 RequestId = NextRouteRequestId++;
    if (!MRpc::CallRemote(
            RouterServerConn,
            "MRouterServer",
            "Rpc_OnPeerRouteQuery",
            Config.OwnerServerId,
            RequestId,
            static_cast<uint8>(EServerType::Mgo),
            static_cast<uint64>(0),
            static_cast<uint16>(0)))
    {
        LOG_WARN("World->Router mgo route query RPC send failed (request=%llu)",
                 static_cast<unsigned long long>(RequestId));
    }
}

void MWorldServer::SendLoadReport()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    uint32 OnlineCount = 0;
    for (const auto& [PlayerId, Avatar] : Players)
    {
        (void)PlayerId;
        MPlayerSession* Player = Avatar ? Avatar->GetPlayerSession() : nullptr;
        if (Player && Player->IsOnline())
        {
            ++OnlineCount;
        }
    }

    if (!MRpc::CallRemote(
            RouterServerConn,
            "MRouterServer",
            "Rpc_OnPeerServerLoadReport",
            Config.OwnerServerId,
            OnlineCount,
            Config.MaxPlayers))
    {
        LOG_WARN("World->Router load report RPC send failed");
    }
}

void MWorldServer::ApplyLoginServerRoute(uint32 ServerId, const MString& ServerName, const MString& Address, uint16 Port)
{
    if (!LoginServerConn)
    {
        return;
    }

    const SServerConnectionConfig& CurrentConfig = LoginServerConn->GetConfig();
    const bool bRouteChanged =
        CurrentConfig.ServerId != ServerId ||
        CurrentConfig.ServerName != ServerName ||
        CurrentConfig.Address != Address ||
        CurrentConfig.Port != Port;

    if (bRouteChanged && (LoginServerConn->IsConnected() || LoginServerConn->IsConnecting()))
    {
        LoginServerConn->Disconnect();
    }

    SServerConnectionConfig NewConfig(ServerId, EServerType::Login, ServerName, Address, Port);
    NewConfig.HeartbeatInterval = CurrentConfig.HeartbeatInterval;
    NewConfig.ConnectTimeout = CurrentConfig.ConnectTimeout;
    NewConfig.ReconnectInterval = CurrentConfig.ReconnectInterval;
    LoginServerConn->SetConfig(NewConfig);

    if (!LoginServerConn->IsConnected() && !LoginServerConn->IsConnecting())
    {
        LoginServerConn->Connect();
    }
}

void MWorldServer::ApplyMgoServerRoute(uint32 ServerId, const MString& ServerName, const MString& Address, uint16 Port)
{
    if (!MgoServerConn)
    {
        return;
    }

    const SServerConnectionConfig& CurrentConfig = MgoServerConn->GetConfig();
    const bool bRouteChanged =
        CurrentConfig.ServerId != ServerId ||
        CurrentConfig.ServerName != ServerName ||
        CurrentConfig.Address != Address ||
        CurrentConfig.Port != Port;

    if (bRouteChanged && (MgoServerConn->IsConnected() || MgoServerConn->IsConnecting()))
    {
        MgoServerConn->Disconnect();
    }

    SServerConnectionConfig NewConfig(ServerId, EServerType::Mgo, ServerName, Address, Port);
    NewConfig.HeartbeatInterval = CurrentConfig.HeartbeatInterval;
    NewConfig.ConnectTimeout = CurrentConfig.ConnectTimeout;
    NewConfig.ReconnectInterval = CurrentConfig.ReconnectInterval;
    MgoServerConn->SetConfig(NewConfig);

    if (!MgoServerConn->IsConnected() && !MgoServerConn->IsConnecting())
    {
        MgoServerConn->Connect();
    }
}
