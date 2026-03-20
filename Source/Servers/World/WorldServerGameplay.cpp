#include "WorldServer.h"
#include "Servers/World/Avatar/InventoryMember.h"
#include "Servers/World/Avatar/PlayerAvatar.h"
#include "Common/Net/NetMessages.h"
#include <sstream>

void MWorldServer::HandleGameplayPacket(uint64 PlayerId, const TByteArray& Data)
{
    if (Data.empty())
    {
        return;
    }

    const EClientMessageType MsgType = (EClientMessageType)Data[0];

    switch (MsgType)
    {
        case EClientMessageType::MT_PlayerMove:
        {
            TByteArray Payload(Data.begin() + 1, Data.end());
            SPlayerMovePayload MovePayload;
            auto ParseResult = ParsePayload(Payload, MovePayload, "MT_PlayerMove");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            auto* Player = GetPlayerById(PlayerId);
            if (!Player || !Player->Avatar)
            {
                return;
            }

            const SVector NewPos(MovePayload.X, MovePayload.Y, MovePayload.Z);
            Player->Avatar->SetLocation(NewPos);

            const SPlayerSceneStateMessage Message{
                Player->PlayerId,
                static_cast<uint16>(Player->CurrentSceneId),
                NewPos.X,
                NewPos.Y,
                NewPos.Z
            };
            BroadcastToScenes((uint8)EServerMessageType::MT_PlayerDataSync, BuildPayload(Message));

            LOG_DEBUG("Player %llu moved to (%.2f, %.2f, %.2f)",
                     (unsigned long long)Player->PlayerId, NewPos.X, NewPos.Y, NewPos.Z);
            break;
        }
        case EClientMessageType::MT_Chat:
        {
            TByteArray Payload(Data.begin() + 1, Data.end());
            SChatBroadcastPayload ChatPayload;
            auto ParseResult = ParsePayload(Payload, ChatPayload, "MT_Chat");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            if (ChatPayload.Message.empty())
            {
                LOG_WARN("Ignoring empty MT_Chat from player %llu", (unsigned long long)PlayerId);
                return;
            }

            auto SendChatToPlayer = [this](uint64 InTargetPlayerId, uint64 InFromPlayerId, const MString& InMessage)
            {
                auto TargetIt = Players.find(InTargetPlayerId);
                if (TargetIt == Players.end() || !TargetIt->second.bOnline || TargetIt->second.GatewayConnectionId == 0)
                {
                    return;
                }
                const SChatMessage OutgoingChat{InFromPlayerId, InMessage};
                const TByteArray ChatPayloadBytes = BuildPayload(OutgoingChat);
                TByteArray ChatPacket;
                ChatPacket.reserve(1 + ChatPayloadBytes.size());
                ChatPacket.push_back(static_cast<uint8>(EClientMessageType::MT_Chat));
                ChatPacket.insert(ChatPacket.end(), ChatPayloadBytes.begin(), ChatPayloadBytes.end());
                SendServerMessage(
                    TargetIt->second.GatewayConnectionId,
                    EServerMessageType::MT_PlayerClientSync,
                    SPlayerClientSyncMessage{InTargetPlayerId, ChatPacket});
            };

            if (ChatPayload.Message.rfind("/bag", 0) == 0)
            {
                MPlayerSession* Player = GetPlayerById(PlayerId);
                if (!Player || !Player->Avatar)
                {
                    return;
                }

                MInventoryMember* Inventory = Player->Avatar->GetRequiredMember<MInventoryMember>();
                if (!Inventory)
                {
                    SendChatToPlayer(PlayerId, 0, "[bag] inventory member missing");
                    return;
                }

                std::istringstream SS(ChatPayload.Message);
                MString Cmd;
                MString Action;
                SS >> Cmd >> Action;

                if (Action == "add")
                {
                    uint32 ItemId = 0;
                    SS >> ItemId;
                    if (ItemId == 0)
                    {
                        SendChatToPlayer(PlayerId, 0, "[bag] usage: /bag add <item_id>");
                        return;
                    }
                    const bool bAdded = Inventory->AddItem(ItemId);
                    if (!bAdded)
                    {
                        SendChatToPlayer(PlayerId, 0, "[bag] add failed: bag full or invalid item");
                        return;
                    }
                    PersistenceSubsystem.EnqueueIfDirty(Inventory, Inventory->GetClass());
                    (void)SendInventoryPullToPlayer(PlayerId);
                    SendChatToPlayer(
                        PlayerId,
                        0,
                        "[bag] add ok: item=" + MStringUtil::ToString(ItemId) +
                        " count=" + MStringUtil::ToString(static_cast<uint64>(Inventory->GetItemCount(ItemId))));
                    return;
                }
                if (Action == "del")
                {
                    uint32 ItemId = 0;
                    SS >> ItemId;
                    if (ItemId == 0)
                    {
                        SendChatToPlayer(PlayerId, 0, "[bag] usage: /bag del <item_id>");
                        return;
                    }
                    const bool bRemoved = Inventory->RemoveItem(ItemId);
                    if (bRemoved)
                    {
                        PersistenceSubsystem.EnqueueIfDirty(Inventory, Inventory->GetClass());
                        (void)SendInventoryPullToPlayer(PlayerId);
                    }
                    SendChatToPlayer(PlayerId, 0, bRemoved
                        ? ("[bag] del ok: item=" + MStringUtil::ToString(ItemId))
                        : ("[bag] del miss: item=" + MStringUtil::ToString(ItemId)));
                    return;
                }
                if (Action == "gold")
                {
                    int32 Delta = 0;
                    SS >> Delta;
                    if (Delta >= 0)
                    {
                        Inventory->AddGold(Delta);
                    }
                    else
                    {
                        (void)Inventory->SpendGold(-Delta);
                    }
                    PersistenceSubsystem.EnqueueIfDirty(Inventory, Inventory->GetClass());
                    (void)SendInventoryPullToPlayer(PlayerId);
                    SendChatToPlayer(PlayerId, 0, "[bag] gold=" + MStringUtil::ToString(Inventory->GetGold()));
                    return;
                }
                if (Action == "show")
                {
                    if (!SendInventoryPullToPlayer(PlayerId))
                    {
                        SendChatToPlayer(PlayerId, 0, "[bag] pull failed");
                    }
                    else
                    {
                        SendChatToPlayer(PlayerId, 0, "[bag] " + Inventory->BuildSummary());
                    }
                    return;
                }

                SendChatToPlayer(PlayerId, 0, "[bag] commands: /bag add|del|gold|show");
                return;
            }

            const SChatMessage OutgoingChat{PlayerId, ChatPayload.Message};
            const TByteArray ChatPayloadBytes = BuildPayload(OutgoingChat);
            TByteArray ChatPacket;
            ChatPacket.reserve(1 + ChatPayloadBytes.size());
            ChatPacket.push_back(static_cast<uint8>(EClientMessageType::MT_Chat));
            ChatPacket.insert(ChatPacket.end(), ChatPayloadBytes.begin(), ChatPayloadBytes.end());

            uint32 DeliveredCount = 0;
            for (const auto& [TargetPlayerId, TargetPlayer] : Players)
            {
                if (!TargetPlayer.bOnline || TargetPlayer.GatewayConnectionId == 0)
                {
                    continue;
                }

                const bool bSent = SendServerMessage(
                    TargetPlayer.GatewayConnectionId,
                    EServerMessageType::MT_PlayerClientSync,
                    SPlayerClientSyncMessage{TargetPlayerId, ChatPacket});
                if (bSent)
                {
                    ++DeliveredCount;
                }
            }

            LOG_INFO("Player %llu chat broadcast delivered to %u player(s): %s",
                     (unsigned long long)PlayerId,
                     static_cast<unsigned>(DeliveredCount),
                     ChatPayload.Message.c_str());
            break;
        }
        default:
            LOG_DEBUG("Unknown message type: %d", (int)MsgType);
            break;
    }
}
