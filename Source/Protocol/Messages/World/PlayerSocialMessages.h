#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Servers/App/ServerCallRequestValidation.h"

MSTRUCT()
struct FPlayerOpenTradeSessionRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerOpenTradeSession"))
    uint64 PlayerId = 0;

    MPROPERTY(Meta=(NonZero, ErrorCode="trade_target_player_required", ErrorContext="PlayerOpenTradeSession"))
    uint64 TargetPlayerId = 0;

    MPROPERTY(Meta=(NonZero, ErrorCode="trade_witness_player_required", ErrorContext="PlayerOpenTradeSession"))
    uint64 WitnessPlayerId = 0;
};

MSTRUCT()
struct FPlayerOpenTradeSessionResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 TradeSessionId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;

    MPROPERTY()
    uint64 WitnessPlayerId = 0;
};

MSTRUCT()
struct FPlayerConfirmTradeRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerConfirmTrade"))
    uint64 PlayerId = 0;

    MPROPERTY(Meta=(NonZero, ErrorCode="trade_session_id_required", ErrorContext="PlayerConfirmTrade"))
    uint64 TradeSessionId = 0;
};

MSTRUCT()
struct FPlayerConfirmTradeResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 TradeSessionId = 0;

    MPROPERTY()
    uint32 ConfirmedCount = 0;

    MPROPERTY()
    bool bAllConfirmed = false;
};

MSTRUCT()
struct FPlayerCreatePartyRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerCreateParty"))
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerCreatePartyResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 LeaderPlayerId = 0;

    MPROPERTY()
    uint32 MemberCount = 0;
};

MSTRUCT()
struct FPlayerInvitePartyRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerInviteParty"))
    uint64 PlayerId = 0;

    MPROPERTY(Meta=(NonZero, ErrorCode="party_target_player_required", ErrorContext="PlayerInviteParty"))
    uint64 TargetPlayerId = 0;
};

MSTRUCT()
struct FPlayerInvitePartyResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;
};

MSTRUCT()
struct FPlayerAcceptPartyInviteRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerAcceptPartyInvite"))
    uint64 PlayerId = 0;

    MPROPERTY(Meta=(NonZero, ErrorCode="party_id_required", ErrorContext="PlayerAcceptPartyInvite"))
    uint64 PartyId = 0;
};

MSTRUCT()
struct FPlayerAcceptPartyInviteResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 LeaderPlayerId = 0;

    MPROPERTY()
    uint32 MemberCount = 0;
};

MSTRUCT()
struct FPlayerKickPartyMemberRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerKickPartyMember"))
    uint64 PlayerId = 0;

    MPROPERTY(Meta=(NonZero, ErrorCode="party_id_required", ErrorContext="PlayerKickPartyMember"))
    uint64 PartyId = 0;

    MPROPERTY(Meta=(NonZero, ErrorCode="party_target_player_required", ErrorContext="PlayerKickPartyMember"))
    uint64 TargetPlayerId = 0;
};

MSTRUCT()
struct FPlayerKickPartyMemberResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 PartyId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;

    MPROPERTY()
    uint32 MemberCount = 0;
};
