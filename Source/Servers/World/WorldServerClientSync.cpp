#include "WorldServer.h"
#include "Servers/World/Avatar/InventoryMember.h"
#include "Servers/World/Avatar/PlayerAvatar.h"

bool MWorldServer::SendClientFunctionPacketToPlayer(uint64 PlayerId, const TByteArray& Packet)
{
    auto TargetIt = Players.find(PlayerId);
    if (TargetIt == Players.end() || !TargetIt->second.bOnline || TargetIt->second.GatewayConnectionId == 0)
    {
        return false;
    }

    return SendServerMessage(
        TargetIt->second.GatewayConnectionId,
        EServerMessageType::MT_PlayerClientSync,
        SPlayerClientSyncMessage{PlayerId, Packet});
}

bool MWorldServer::SendInventoryPullToPlayer(uint64 PlayerId)
{
    MPlayerSession* Player = GetPlayerById(PlayerId);
    if (!Player || !Player->Avatar)
    {
        return false;
    }

    MInventoryMember* Inventory = Player->Avatar->GetRequiredMember<MInventoryMember>();
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
