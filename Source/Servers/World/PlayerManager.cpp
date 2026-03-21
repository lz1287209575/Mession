#include "WorldServer.h"
#include "Common/Net/ServerRpcRuntime.h"
#include "Common/Runtime/HexUtils.h"
#include "Servers/World/Avatar/PlayerAvatar.h"

namespace
{
class MGatewayClientTunnelConnection : public INetConnection
{
public:
    MGatewayClientTunnelConnection(
        TFunction<bool(const void*, uint32)> InSendCallback,
        TFunction<bool()> InIsConnectedCallback)
        : SendCallback(InSendCallback)
        , IsConnectedCallback(InIsConnectedCallback)
    {
    }

    bool Send(const void* Data, uint32 Size) override
    {
        if (!Data || Size == 0 || !SendCallback)
        {
            return false;
        }

        return SendCallback(Data, Size);
    }

    bool Receive(void* /*Buffer*/, uint32 /*BufferSize*/, uint32& OutBytesReceived) override
    {
        OutBytesReceived = 0;
        return false;
    }

    uint64 GetPlayerId() const override
    {
        return PlayerId;
    }

    void SetPlayerId(uint64 Id) override
    {
        PlayerId = Id;
    }

    bool ReceivePacket(TByteArray& /*OutPayload*/) override
    {
        return false;
    }

    void Close() override
    {
    }

    bool IsConnected() const override
    {
        return IsConnectedCallback ? IsConnectedCallback() : false;
    }

    TSocketFd GetSocketFd() const override
    {
        return INVALID_SOCKET_FD;
    }

    void SetNonBlocking(bool /*bNonBlocking*/) override
    {
    }

private:
    uint64 PlayerId = 0;
    TFunction<bool(const void*, uint32)> SendCallback;
    TFunction<bool()> IsConnectedCallback;
};
}

MPlayerAvatar* MWorldServer::CreateRuntimePlayer(uint64 PlayerId, uint64 GatewayConnectionId, uint32 SessionKey)
{
    if (Players.find(PlayerId) != Players.end())
    {
        return nullptr;
    }

    MPlayerAvatar* Avatar = NewMObject<MPlayerAvatar>(this, "PlayerAvatar");
    Avatar->SetOwnerPlayerId(PlayerId);
    Avatar->SetLocation(SVector(-1040, 0, 90));

    MPlayerSession* Player = Avatar->GetPlayerSession();
    if (Player)
    {
        Player->PlayerId = PlayerId;
        Player->SetGatewayConnectionId(GatewayConnectionId);
        Player->SetSessionKey(SessionKey);
        Player->SetCurrentSceneId(1);
        Player->SetOnline(true);
    }

    Players[PlayerId] = Avatar;
    return Avatar;
}

void MWorldServer::EnterWorld(MPlayerAvatar* Avatar)
{
    if (!Avatar)
    {
        return;
    }

    MPlayerSession* Player = Avatar->GetPlayerSession();
    if (!Player)
    {
        return;
    }

    const uint64 PlayerId = Player->PlayerId;
    const uint64 GatewayConnectionId = Player->GetGatewayConnectionId();

    TSharedPtr<INetConnection> ReplicationConnection = MakeShared<MGatewayClientTunnelConnection>(
        [this, GatewayConnectionId, PlayerId](const void* Data, uint32 Size) -> bool
        {
            TByteArray PacketBytes;
            const uint8* ByteData = static_cast<const uint8*>(Data);
            PacketBytes.insert(PacketBytes.end(), ByteData, ByteData + Size);
            auto GatewayIt = BackendConnections.find(GatewayConnectionId);
            const bool bOk =
                GatewayIt != BackendConnections.end() &&
                GatewayIt->second.Connection &&
                MRpc::CallRemote(
                    GatewayIt->second.Connection,
                    "MGatewayServer",
                    "Rpc_OnPlayerClientSync",
                    PlayerId,
                    Hex::BytesToHex(PacketBytes));
            if (!bOk)
            {
                LOG_WARN("Tunnel send to gateway conn %llu for player %llu failed",
                         (unsigned long long)GatewayConnectionId,
                         (unsigned long long)PlayerId);
            }
            return bOk;
        },
        [this, GatewayConnectionId]() -> bool
        {
            auto GatewayIt = BackendConnections.find(GatewayConnectionId);
            return GatewayIt != BackendConnections.end() &&
                GatewayIt->second.bAuthenticated &&
                GatewayIt->second.ServerType == EServerType::Gateway &&
                GatewayIt->second.Connection &&
                GatewayIt->second.Connection->IsConnected();
        });
    ReplicationConnection->SetPlayerId(PlayerId);

    ReplicationDriver->RegisterActor(Avatar);
    ReplicationDriver->AddConnection(PlayerId, ReplicationConnection);
    ReplicationDriver->AddRelevantActor(PlayerId, Avatar->GetObjectId());
    ReplicationDriver->BroadcastActorCreate(Avatar);

    const uint16 SceneId = static_cast<uint16>(Player ? Player->GetCurrentSceneId() : 0u);
    const SPlayerSceneStateMessage Message{
        PlayerId,
        SceneId,
        Avatar->GetLocation().X,
        Avatar->GetLocation().Y,
        Avatar->GetLocation().Z
    };
    for (auto& [ConnectionId, Peer] : BackendConnections)
    {
        (void)ConnectionId;
        if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Scene || !Peer.Connection)
        {
            continue;
        }

        if (!MRpc::CallRemote(
                Peer.Connection,
                "MSceneServer",
                "Rpc_OnPlayerSwitchServer",
                Message.PlayerId,
                Message.SceneId,
                Message.X,
                Message.Y,
                Message.Z))
        {
            LOG_WARN("World->Scene player switch RPC send failed (player=%llu scene=%u)",
                     static_cast<unsigned long long>(Message.PlayerId),
                     static_cast<unsigned>(Message.SceneId));
        }
    }

    LOG_INFO("Player %s (id=%llu) added to world",
             Avatar->GetDisplayName().empty() ? "<unset>" : Avatar->GetDisplayName().c_str(),
             (unsigned long long)PlayerId);
}

void MWorldServer::RemovePlayer(uint64 PlayerId)
{
    auto It = Players.find(PlayerId);
    if (It == Players.end())
    {
        return;
    }

    MPlayerAvatar* Avatar = It->second;
    MPlayerSession* Player = Avatar ? Avatar->GetPlayerSession() : nullptr;

    ReplicationDriver->RemoveConnection(PlayerId);
    if (Avatar)
    {
        ReplicationDriver->BroadcastActorDestroy(Avatar->GetObjectId());
        delete Avatar;
    }
    const uint16 SceneId = static_cast<uint16>(Player ? Player->GetCurrentSceneId() : 0u);
    for (auto& [ConnectionId, Peer] : BackendConnections)
    {
        (void)ConnectionId;
        if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Scene || !Peer.Connection)
        {
            continue;
        }

        if (!MRpc::CallRemote(
                Peer.Connection,
                "MSceneServer",
                "Rpc_OnPlayerLogout",
                PlayerId,
                SceneId))
        {
            LOG_WARN("World->Scene player logout RPC send failed (player=%llu scene=%u)",
                     static_cast<unsigned long long>(PlayerId),
                     static_cast<unsigned>(SceneId));
        }
    }

    Players.erase(It);

    LOG_INFO("Player %llu removed from world", (unsigned long long)PlayerId);
}

MPlayerSession* MWorldServer::GetPlayerById(uint64 PlayerId)
{
    MPlayerAvatar* Avatar = GetPlayerAvatarById(PlayerId);
    return Avatar ? Avatar->GetPlayerSession() : nullptr;
}

MPlayerAvatar* MWorldServer::GetPlayerAvatarById(uint64 PlayerId)
{
    auto It = Players.find(PlayerId);
    return (It != Players.end()) ? It->second : nullptr;
}

void MWorldServer::UpdateGameLogic(float DeltaTime)
{
    for (auto& [PlayerId, Avatar] : Players)
    {
        (void)PlayerId;
        if (Avatar)
        {
            static_cast<MActor*>(Avatar)->Tick(DeltaTime);
            PersistenceSubsystem.EnqueueIfDirty(Avatar, Avatar->GetClass());
            for (MAvatarMember* Member : Avatar->GetMembers())
            {
                if (!Member)
                {
                    continue;
                }
                PersistenceSubsystem.EnqueueIfDirty(Member, Member->GetClass());
            }
        }
    }
}
