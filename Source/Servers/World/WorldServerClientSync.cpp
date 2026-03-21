#include "WorldServer.h"
#include "Common/Net/ServerRpcRuntime.h"
#include "Common/Runtime/HexUtils.h"
#include "Servers/World/Avatar/InventoryMember.h"
#include "Servers/World/Avatar/PlayerAvatar.h"

bool MWorldServer::SendClientFunctionPacketToPlayer(uint64 PlayerId, const TByteArray& Packet)
{
    MPlayerSession* Player = GetPlayerById(PlayerId);
    if (!Player || !Player->IsOnline() || Player->GetGatewayConnectionId() == 0)
    {
        return false;
    }

    auto GatewayIt = BackendConnections.find(Player->GetGatewayConnectionId());
    if (GatewayIt == BackendConnections.end() || !GatewayIt->second.Connection)
    {
        return false;
    }

    return MRpc::CallRemote(
        GatewayIt->second.Connection,
        "MGatewayServer",
        "Rpc_OnPlayerClientSync",
        PlayerId,
        Hex::BytesToHex(Packet));
}

bool MWorldServer::SendInventoryPullToPlayer(uint64 PlayerId)
{
    MPlayerSession* Player = GetPlayerById(PlayerId);
    MPlayerAvatar* Avatar = GetPlayerAvatarById(PlayerId);
    if (!Player || !Avatar)
    {
        return false;
    }

    MInventoryMember* Inventory = Avatar->GetRequiredMember<MInventoryMember>();
    if (!Inventory)
    {
        return false;
    }

    SInventorySnapshotPayload Payload;
    Payload.PlayerId = PlayerId;
    Payload.Gold = Inventory->GetGold();
    Payload.MaxSlots = Inventory->GetMaxSlots();
    Payload.Items.reserve(Inventory->GetItems().size());
    for (const SInventoryItem& Item : Inventory->GetItems())
    {
        Payload.Items.push_back(SInventoryItemPayload{
            Item.InstanceId,
            Item.ItemId,
            Item.Count,
            Item.bBound,
            Item.ExpireAtUnixSeconds,
            Item.Flags,
        });
    }

    TByteArray Packet;
    if (!BuildClientFunctionCallPacketForPayload(MClientDownlink::Id_OnInventoryPull(), Payload, Packet))
    {
        return false;
    }

    return SendClientFunctionPacketToPlayer(PlayerId, Packet);
}
