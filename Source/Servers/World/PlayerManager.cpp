#include "WorldServer.h"
#include "Gameplay/PlayerAvatar.h"

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

void MWorldServer::AddPlayer(uint64 PlayerId, const MString& Name, uint64 GatewayConnectionId)
{
    if (Players.find(PlayerId) != Players.end())
    {
        return;
    }

    MPlayerSession Player;
    Player.PlayerId = PlayerId;
    Player.Name = Name;
    Player.GatewayConnectionId = GatewayConnectionId;
    Player.CurrentSceneId = 1;
    Player.bOnline = true;

    MPlayerAvatar* Avatar = new MPlayerAvatar();
    Avatar->SetOwnerPlayerId(PlayerId);
    Avatar->SetDisplayName(Name);
    Avatar->SetLocation(SVector(-1040, 0, 90));

    Player.Avatar = Avatar;
    Players[PlayerId] = Player;
    MPlayerSession& StoredSession = Players[PlayerId];
    Avatar->SetPlayerSession(&StoredSession);

    TSharedPtr<INetConnection> ReplicationConnection = MakeShared<MGatewayClientTunnelConnection>(
        [this, GatewayConnectionId, PlayerId](const void* Data, uint32 Size) -> bool
        {
            TByteArray PacketBytes;
            const uint8* ByteData = static_cast<const uint8*>(Data);
            PacketBytes.insert(PacketBytes.end(), ByteData, ByteData + Size);
            const bool bOk = SendServerMessage(
                GatewayConnectionId,
                EServerMessageType::MT_PlayerClientSync,
                SPlayerClientSyncMessage{PlayerId, PacketBytes});
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

    const SPlayerSceneStateMessage Message{
        Player.PlayerId,
        static_cast<uint16>(Player.CurrentSceneId),
        Player.Avatar->GetLocation().X,
        Player.Avatar->GetLocation().Y,
        Player.Avatar->GetLocation().Z
    };
    BroadcastToScenes((uint8)EServerMessageType::MT_PlayerSwitchServer, BuildPayload(Message));

    LOG_INFO("Player %s (id=%llu) added to world",
             Name.c_str(),
             (unsigned long long)PlayerId);
}

void MWorldServer::RemovePlayer(uint64 PlayerId)
{
    auto It = Players.find(PlayerId);
    if (It == Players.end())
    {
        return;
    }

    MPlayerSession& Player = It->second;

    ReplicationDriver->RemoveConnection(Player.PlayerId);
    if (Player.Avatar)
    {
        Player.Avatar->SetPlayerSession(nullptr);
        ReplicationDriver->BroadcastActorDestroy(Player.Avatar->GetObjectId());
        delete Player.Avatar;
        Player.Avatar = nullptr;
    }
    BroadcastToScenes(
        (uint8)EServerMessageType::MT_PlayerLogout,
        BuildPayload(SPlayerSceneLeaveMessage{Player.PlayerId, static_cast<uint16>(Player.CurrentSceneId)}));

    Players.erase(It);

    LOG_INFO("Player %llu removed from world", (unsigned long long)PlayerId);
}

MPlayerSession* MWorldServer::GetPlayerById(uint64 PlayerId)
{
    auto It = Players.find(PlayerId);
    return (It != Players.end()) ? &It->second : nullptr;
}

void MWorldServer::UpdateGameLogic(float DeltaTime)
{
    for (auto& [PlayerId, Player] : Players)
    {
        (void)PlayerId;
        if (Player.Avatar)
        {
            static_cast<MActor*>(Player.Avatar)->Tick(DeltaTime);
            PersistenceSubsystem.EnqueueIfDirty(Player.Avatar, Player.Avatar->GetClass());
            for (const TUniquePtr<MAvatarMember>& Member : Player.Avatar->GetMembers())
            {
                if (!Member)
                {
                    continue;
                }
                PersistenceSubsystem.EnqueueIfDirty(Member.get(), Member->GetClass());
            }
        }
    }
}
