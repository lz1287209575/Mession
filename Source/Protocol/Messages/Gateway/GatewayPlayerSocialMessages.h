#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/World/PlayerSocialMessages.h"

MSTRUCT()
struct FClientOpenTradeSessionResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 TradeSessionId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;

    MPROPERTY()
    uint64 WitnessPlayerId = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientConfirmTradeResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 TradeSessionId = 0;

    MPROPERTY()
    uint32 ConfirmedCount = 0;

    MPROPERTY()
    bool bAllConfirmed = false;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientCreatePartyResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 LeaderPlayerId = 0;

    MPROPERTY()
    uint32 MemberCount = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientInvitePartyResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientAcceptPartyInviteResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 LeaderPlayerId = 0;

    MPROPERTY()
    uint32 MemberCount = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientKickPartyMemberResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;

    MPROPERTY()
    uint32 MemberCount = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientTradeSessionOpenedNotify
{
    MPROPERTY()
    uint64 TradeSessionId = 0;

    MPROPERTY()
    uint64 InitiatorPlayerId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;

    MPROPERTY()
    uint64 WitnessPlayerId = 0;
};

MSTRUCT()
struct FClientTradeSessionUpdatedNotify
{
    MPROPERTY()
    uint64 TradeSessionId = 0;

    MPROPERTY()
    uint64 ActorPlayerId = 0;

    MPROPERTY()
    uint32 ConfirmedCount = 0;

    MPROPERTY()
    bool bAllConfirmed = false;
};

MSTRUCT()
struct FClientTradeSessionClosedNotify
{
    MPROPERTY()
    uint64 TradeSessionId = 0;

    MPROPERTY()
    uint64 ActorPlayerId = 0;

    MPROPERTY()
    MString Reason;
};

MSTRUCT()
struct FClientPartyCreatedNotify
{
    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 LeaderPlayerId = 0;

    MPROPERTY()
    TVector<uint64> MemberPlayerIds;
};

MSTRUCT()
struct FClientPartyInviteReceivedNotify
{
    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 LeaderPlayerId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;
};

MSTRUCT()
struct FClientPartyMemberJoinedNotify
{
    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 LeaderPlayerId = 0;

    MPROPERTY()
    uint64 JoinedPlayerId = 0;

    MPROPERTY()
    TVector<uint64> MemberPlayerIds;
};

MSTRUCT()
struct FClientPartyMemberRemovedNotify
{
    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 LeaderPlayerId = 0;

    MPROPERTY()
    uint64 RemovedPlayerId = 0;

    MPROPERTY()
    TVector<uint64> MemberPlayerIds;

    MPROPERTY()
    MString Reason;
};

MSTRUCT()
struct FClientPartyDisbandedNotify
{
    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 LeaderPlayerId = 0;

    MPROPERTY()
    uint64 ActorPlayerId = 0;

    MPROPERTY()
    TVector<uint64> MemberPlayerIds;

    MPROPERTY()
    MString Reason;
};
