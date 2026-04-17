#include "Servers/World/Player/PlayerService.h"

#include "Servers/World/Player/Player.h"

namespace
{
template<typename TResponse>
TResult<TResponse, FAppError> MakeSocialError(const char* Code, const char* Message = "")
{
    return MakeErrorResult<TResponse>(FAppError::Make(
        Code ? Code : "player_social_command_failed",
        Message ? Message : ""));
}

bool ContainsPlayerId(const TVector<uint64>& Values, uint64 PlayerId)
{
    return std::find(Values.begin(), Values.end(), PlayerId) != Values.end();
}

void AppendParticipant(TVector<SPlayerCommandParticipant>& Participants, uint64 PlayerId)
{
    if (PlayerId != 0)
    {
        Participants.push_back(SPlayerCommandParticipant{PlayerId, 0, true});
    }
}

void AppendParticipants(TVector<SPlayerCommandParticipant>& Participants, const TVector<uint64>& PlayerIds)
{
    for (uint64 PlayerId : PlayerIds)
    {
        AppendParticipant(Participants, PlayerId);
    }
}

void ErasePlayerId(TVector<uint64>& Values, uint64 PlayerId)
{
    Values.erase(
        std::remove(Values.begin(), Values.end(), PlayerId),
        Values.end());
}

template<typename TResponse>
MFuture<TResult<TResponse, FAppError>> MakeSocialErrorFuture(const char* Code, const char* Message = "")
{
    return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(Code, Message);
}
}

MFuture<TResult<FPlayerOpenTradeSessionResponse, FAppError>> MPlayerService::PlayerOpenTradeSession(
    const FPlayerOpenTradeSessionRequest& Request)
{
    if (Request.PlayerId == Request.TargetPlayerId ||
        Request.PlayerId == Request.WitnessPlayerId ||
        Request.TargetPlayerId == Request.WitnessPlayerId)
    {
        return MakeSocialErrorFuture<FPlayerOpenTradeSessionResponse>(
            "trade_participants_must_be_unique",
            "PlayerOpenTradeSession");
    }

    TVector<SPlayerCommandParticipant> Participants;
    Participants.push_back(SPlayerCommandParticipant{Request.PlayerId, 0, true});
    Participants.push_back(SPlayerCommandParticipant{Request.TargetPlayerId, 0, true});
    Participants.push_back(SPlayerCommandParticipant{Request.WitnessPlayerId, 0, true});
    return DispatchRuntimeCommandMany<FPlayerOpenTradeSessionResponse>(
        Request,
        std::move(Participants),
        "PlayerOpenTradeSession",
        {},
        &MPlayerService::DoPlayerOpenTradeSession);
}

MFuture<TResult<FPlayerConfirmTradeResponse, FAppError>> MPlayerService::PlayerConfirmTrade(
    const FPlayerConfirmTradeRequest& Request)
{
    TVector<SPlayerCommandParticipant> Participants;
    {
        std::lock_guard<std::mutex> Lock(SocialStateMutex);
        const auto TradeIt = TradeSessions.find(Request.TradeSessionId);
        if (TradeIt == TradeSessions.end())
        {
            return MakeSocialErrorFuture<FPlayerConfirmTradeResponse>(
                "trade_session_not_found",
                "PlayerConfirmTrade");
        }

        const STradeSessionState& Session = TradeIt->second;
        Participants.push_back(SPlayerCommandParticipant{Session.InitiatorPlayerId, 0, true});
        Participants.push_back(SPlayerCommandParticipant{Session.TargetPlayerId, 0, true});
        Participants.push_back(SPlayerCommandParticipant{Session.WitnessPlayerId, 0, true});
    }

    return DispatchRuntimeCommandMany<FPlayerConfirmTradeResponse>(
        Request,
        std::move(Participants),
        "PlayerConfirmTrade",
        {},
        &MPlayerService::DoPlayerConfirmTrade);
}

MFuture<TResult<FPlayerCreatePartyResponse, FAppError>> MPlayerService::PlayerCreateParty(
    const FPlayerCreatePartyRequest& Request)
{
    return DispatchRuntimeCommand<FPlayerCreatePartyResponse>(
        Request,
        "PlayerCreateParty",
        {},
        &MPlayerService::DoPlayerCreateParty);
}

MFuture<TResult<FPlayerInvitePartyResponse, FAppError>> MPlayerService::PlayerInviteParty(
    const FPlayerInvitePartyRequest& Request)
{
    if (Request.PlayerId == Request.TargetPlayerId)
    {
        return MakeSocialErrorFuture<FPlayerInvitePartyResponse>(
            "party_target_player_invalid",
            "PlayerInviteParty");
    }

    TVector<SPlayerCommandParticipant> Participants;
    Participants.push_back(SPlayerCommandParticipant{Request.PlayerId, 0, true});
    Participants.push_back(SPlayerCommandParticipant{Request.TargetPlayerId, 0, true});
    return DispatchRuntimeCommandMany<FPlayerInvitePartyResponse>(
        Request,
        std::move(Participants),
        "PlayerInviteParty",
        {},
        &MPlayerService::DoPlayerInviteParty);
}

MFuture<TResult<FPlayerAcceptPartyInviteResponse, FAppError>> MPlayerService::PlayerAcceptPartyInvite(
    const FPlayerAcceptPartyInviteRequest& Request)
{
    TVector<SPlayerCommandParticipant> Participants;
    {
        std::lock_guard<std::mutex> Lock(SocialStateMutex);
        const auto InviteIt = PendingPartyInviteIds.find(Request.PlayerId);
        if (InviteIt == PendingPartyInviteIds.end() || InviteIt->second != Request.PartyId)
        {
            return MakeSocialErrorFuture<FPlayerAcceptPartyInviteResponse>(
                "party_invite_not_found",
                "PlayerAcceptPartyInvite");
        }

        const auto PartyIt = Parties.find(Request.PartyId);
        if (PartyIt == Parties.end())
        {
            return MakeSocialErrorFuture<FPlayerAcceptPartyInviteResponse>(
                "party_not_found",
                "PlayerAcceptPartyInvite");
        }

        for (uint64 MemberPlayerId : PartyIt->second.MemberPlayerIds)
        {
            Participants.push_back(SPlayerCommandParticipant{MemberPlayerId, 0, true});
        }
        Participants.push_back(SPlayerCommandParticipant{Request.PlayerId, 0, true});
    }

    return DispatchRuntimeCommandMany<FPlayerAcceptPartyInviteResponse>(
        Request,
        std::move(Participants),
        "PlayerAcceptPartyInvite",
        {},
        &MPlayerService::DoPlayerAcceptPartyInvite);
}

MFuture<TResult<FPlayerKickPartyMemberResponse, FAppError>> MPlayerService::PlayerKickPartyMember(
    const FPlayerKickPartyMemberRequest& Request)
{
    if (Request.PlayerId == Request.TargetPlayerId)
    {
        return MakeSocialErrorFuture<FPlayerKickPartyMemberResponse>(
            "party_target_player_invalid",
            "PlayerKickPartyMember");
    }

    TVector<SPlayerCommandParticipant> Participants;
    {
        std::lock_guard<std::mutex> Lock(SocialStateMutex);
        const auto PartyIt = Parties.find(Request.PartyId);
        if (PartyIt == Parties.end())
        {
            return MakeSocialErrorFuture<FPlayerKickPartyMemberResponse>(
                "party_not_found",
                "PlayerKickPartyMember");
        }

        for (uint64 MemberPlayerId : PartyIt->second.MemberPlayerIds)
        {
            Participants.push_back(SPlayerCommandParticipant{MemberPlayerId, 0, true});
        }
    }

    return DispatchRuntimeCommandMany<FPlayerKickPartyMemberResponse>(
        Request,
        std::move(Participants),
        "PlayerKickPartyMember",
        {},
        &MPlayerService::DoPlayerKickPartyMember);
}

TResult<FPlayerOpenTradeSessionResponse, FAppError> MPlayerService::DoPlayerOpenTradeSession(
    FPlayerOpenTradeSessionRequest Request)
{
    if (!FindPlayer(Request.PlayerId) ||
        !FindPlayer(Request.TargetPlayerId) ||
        !FindPlayer(Request.WitnessPlayerId))
    {
        return MakeSocialError<FPlayerOpenTradeSessionResponse>(
            "player_not_found",
            "PlayerOpenTradeSession");
    }

    STradeSessionState Session;
    {
        std::lock_guard<std::mutex> Lock(SocialStateMutex);
        if (PlayerTradeSessionIds.contains(Request.PlayerId) ||
            PlayerTradeSessionIds.contains(Request.TargetPlayerId) ||
            PlayerTradeSessionIds.contains(Request.WitnessPlayerId))
        {
            return MakeSocialError<FPlayerOpenTradeSessionResponse>(
                "trade_session_already_active",
                "PlayerOpenTradeSession");
        }

        const uint64 TradeSessionId = NextTradeSessionId++;
        Session.TradeSessionId = TradeSessionId;
        Session.InitiatorPlayerId = Request.PlayerId;
        Session.TargetPlayerId = Request.TargetPlayerId;
        Session.WitnessPlayerId = Request.WitnessPlayerId;
        TradeSessions.emplace(TradeSessionId, Session);
        PlayerTradeSessionIds[Request.PlayerId] = TradeSessionId;
        PlayerTradeSessionIds[Request.TargetPlayerId] = TradeSessionId;
        PlayerTradeSessionIds[Request.WitnessPlayerId] = TradeSessionId;
    }

    FPlayerOpenTradeSessionResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.TradeSessionId = Session.TradeSessionId;
    Response.TargetPlayerId = Request.TargetPlayerId;
    Response.WitnessPlayerId = Request.WitnessPlayerId;
    QueueTradeSessionOpenedNotify(Session);
    return TResult<FPlayerOpenTradeSessionResponse, FAppError>::Ok(std::move(Response));
}

TResult<FPlayerConfirmTradeResponse, FAppError> MPlayerService::DoPlayerConfirmTrade(
    FPlayerConfirmTradeRequest Request)
{
    STradeSessionState Session;
    uint32 ConfirmedCount = 0;
    bool bAllConfirmed = false;
    {
        std::lock_guard<std::mutex> Lock(SocialStateMutex);
        const auto TradeIt = TradeSessions.find(Request.TradeSessionId);
        if (TradeIt == TradeSessions.end())
        {
            return MakeSocialError<FPlayerConfirmTradeResponse>(
                "trade_session_not_found",
                "PlayerConfirmTrade");
        }

        STradeSessionState& MutableSession = TradeIt->second;
        if (Request.PlayerId == MutableSession.InitiatorPlayerId)
        {
            MutableSession.bInitiatorConfirmed = true;
        }
        else if (Request.PlayerId == MutableSession.TargetPlayerId)
        {
            MutableSession.bTargetConfirmed = true;
        }
        else if (Request.PlayerId == MutableSession.WitnessPlayerId)
        {
            MutableSession.bWitnessConfirmed = true;
        }
        else
        {
            return MakeSocialError<FPlayerConfirmTradeResponse>(
                "trade_participant_not_found",
                "PlayerConfirmTrade");
        }

        ConfirmedCount =
            static_cast<uint32>(MutableSession.bInitiatorConfirmed) +
            static_cast<uint32>(MutableSession.bTargetConfirmed) +
            static_cast<uint32>(MutableSession.bWitnessConfirmed);
        bAllConfirmed =
            MutableSession.bInitiatorConfirmed &&
            MutableSession.bTargetConfirmed &&
            MutableSession.bWitnessConfirmed;
        Session = MutableSession;

        if (bAllConfirmed)
        {
            PlayerTradeSessionIds.erase(MutableSession.InitiatorPlayerId);
            PlayerTradeSessionIds.erase(MutableSession.TargetPlayerId);
            PlayerTradeSessionIds.erase(MutableSession.WitnessPlayerId);
            TradeSessions.erase(TradeIt);
        }
    }

    FPlayerConfirmTradeResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.TradeSessionId = Request.TradeSessionId;
    Response.ConfirmedCount = ConfirmedCount;
    Response.bAllConfirmed = bAllConfirmed;
    QueueTradeSessionUpdatedNotify(Session, Request.PlayerId, ConfirmedCount, bAllConfirmed);
    return TResult<FPlayerConfirmTradeResponse, FAppError>::Ok(std::move(Response));
}

TResult<FPlayerCreatePartyResponse, FAppError> MPlayerService::DoPlayerCreateParty(FPlayerCreatePartyRequest Request)
{
    if (!FindPlayer(Request.PlayerId))
    {
        return MakeSocialError<FPlayerCreatePartyResponse>("player_not_found", "PlayerCreateParty");
    }

    SPartyState Party;
    {
        std::lock_guard<std::mutex> Lock(SocialStateMutex);
        if (PlayerPartyIds.contains(Request.PlayerId))
        {
            return MakeSocialError<FPlayerCreatePartyResponse>("party_already_joined", "PlayerCreateParty");
        }

        Party.PartyId = NextPartyId++;
        Party.LeaderPlayerId = Request.PlayerId;
        Party.MemberPlayerIds.push_back(Request.PlayerId);
        Parties.emplace(Party.PartyId, Party);
        PlayerPartyIds[Request.PlayerId] = Party.PartyId;
    }

    FPlayerCreatePartyResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.PartyId = Party.PartyId;
    Response.LeaderPlayerId = Request.PlayerId;
    Response.MemberCount = 1;
    QueuePartyCreatedNotify(Party);
    return TResult<FPlayerCreatePartyResponse, FAppError>::Ok(std::move(Response));
}

TResult<FPlayerInvitePartyResponse, FAppError> MPlayerService::DoPlayerInviteParty(FPlayerInvitePartyRequest Request)
{
    if (!FindPlayer(Request.PlayerId) || !FindPlayer(Request.TargetPlayerId))
    {
        return MakeSocialError<FPlayerInvitePartyResponse>("player_not_found", "PlayerInviteParty");
    }

    SPartyState Party;
    {
        std::lock_guard<std::mutex> Lock(SocialStateMutex);
        const auto OwnerPartyIt = PlayerPartyIds.find(Request.PlayerId);
        if (OwnerPartyIt == PlayerPartyIds.end())
        {
            return MakeSocialError<FPlayerInvitePartyResponse>("party_not_found", "PlayerInviteParty");
        }

        const auto PartyIt = Parties.find(OwnerPartyIt->second);
        if (PartyIt == Parties.end())
        {
            return MakeSocialError<FPlayerInvitePartyResponse>("party_not_found", "PlayerInviteParty");
        }

        SPartyState& MutableParty = PartyIt->second;
        if (MutableParty.LeaderPlayerId != Request.PlayerId)
        {
            return MakeSocialError<FPlayerInvitePartyResponse>("party_leader_required", "PlayerInviteParty");
        }

        if (PlayerPartyIds.contains(Request.TargetPlayerId))
        {
            return MakeSocialError<FPlayerInvitePartyResponse>("party_target_already_joined", "PlayerInviteParty");
        }

        if (PendingPartyInviteIds.contains(Request.TargetPlayerId))
        {
            return MakeSocialError<FPlayerInvitePartyResponse>("party_invite_already_pending", "PlayerInviteParty");
        }

        if (!ContainsPlayerId(MutableParty.PendingInvitePlayerIds, Request.TargetPlayerId))
        {
            MutableParty.PendingInvitePlayerIds.push_back(Request.TargetPlayerId);
        }
        PendingPartyInviteIds[Request.TargetPlayerId] = MutableParty.PartyId;
        Party = MutableParty;
    }

    FPlayerInvitePartyResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.PartyId = Party.PartyId;
    Response.TargetPlayerId = Request.TargetPlayerId;
    QueuePartyInviteNotify(Party, Request.TargetPlayerId);
    return TResult<FPlayerInvitePartyResponse, FAppError>::Ok(std::move(Response));
}

TResult<FPlayerAcceptPartyInviteResponse, FAppError> MPlayerService::DoPlayerAcceptPartyInvite(
    FPlayerAcceptPartyInviteRequest Request)
{
    if (!FindPlayer(Request.PlayerId))
    {
        return MakeSocialError<FPlayerAcceptPartyInviteResponse>("player_not_found", "PlayerAcceptPartyInvite");
    }

    SPartyState Party;
    {
        std::lock_guard<std::mutex> Lock(SocialStateMutex);
        const auto InviteIt = PendingPartyInviteIds.find(Request.PlayerId);
        if (InviteIt == PendingPartyInviteIds.end() || InviteIt->second != Request.PartyId)
        {
            return MakeSocialError<FPlayerAcceptPartyInviteResponse>("party_invite_not_found", "PlayerAcceptPartyInvite");
        }

        const auto PartyIt = Parties.find(Request.PartyId);
        if (PartyIt == Parties.end())
        {
            return MakeSocialError<FPlayerAcceptPartyInviteResponse>("party_not_found", "PlayerAcceptPartyInvite");
        }

        SPartyState& MutableParty = PartyIt->second;
        if (PlayerPartyIds.contains(Request.PlayerId))
        {
            return MakeSocialError<FPlayerAcceptPartyInviteResponse>("party_already_joined", "PlayerAcceptPartyInvite");
        }

        if (!ContainsPlayerId(MutableParty.PendingInvitePlayerIds, Request.PlayerId))
        {
            return MakeSocialError<FPlayerAcceptPartyInviteResponse>("party_invite_not_found", "PlayerAcceptPartyInvite");
        }

        MutableParty.MemberPlayerIds.push_back(Request.PlayerId);
        ErasePlayerId(MutableParty.PendingInvitePlayerIds, Request.PlayerId);
        PendingPartyInviteIds.erase(Request.PlayerId);
        PlayerPartyIds[Request.PlayerId] = MutableParty.PartyId;
        Party = MutableParty;
    }

    FPlayerAcceptPartyInviteResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.PartyId = Party.PartyId;
    Response.LeaderPlayerId = Party.LeaderPlayerId;
    Response.MemberCount = static_cast<uint32>(Party.MemberPlayerIds.size());
    QueuePartyMemberJoinedNotify(Party, Request.PlayerId);
    return TResult<FPlayerAcceptPartyInviteResponse, FAppError>::Ok(std::move(Response));
}

TResult<FPlayerKickPartyMemberResponse, FAppError> MPlayerService::DoPlayerKickPartyMember(
    FPlayerKickPartyMemberRequest Request)
{
    SPartyState Party;
    {
        std::lock_guard<std::mutex> Lock(SocialStateMutex);
        const auto PartyIt = Parties.find(Request.PartyId);
        if (PartyIt == Parties.end())
        {
            return MakeSocialError<FPlayerKickPartyMemberResponse>("party_not_found", "PlayerKickPartyMember");
        }

        SPartyState& MutableParty = PartyIt->second;
        if (MutableParty.LeaderPlayerId != Request.PlayerId)
        {
            return MakeSocialError<FPlayerKickPartyMemberResponse>("party_leader_required", "PlayerKickPartyMember");
        }

        if (!ContainsPlayerId(MutableParty.MemberPlayerIds, Request.TargetPlayerId))
        {
            return MakeSocialError<FPlayerKickPartyMemberResponse>("party_target_not_found", "PlayerKickPartyMember");
        }

        if (Request.TargetPlayerId == MutableParty.LeaderPlayerId)
        {
            return MakeSocialError<FPlayerKickPartyMemberResponse>("party_target_player_invalid", "PlayerKickPartyMember");
        }

        ErasePlayerId(MutableParty.MemberPlayerIds, Request.TargetPlayerId);
        PlayerPartyIds.erase(Request.TargetPlayerId);
        PendingPartyInviteIds.erase(Request.TargetPlayerId);
        ErasePlayerId(MutableParty.PendingInvitePlayerIds, Request.TargetPlayerId);
        Party = MutableParty;
    }

    FPlayerKickPartyMemberResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.PartyId = Request.PartyId;
    Response.TargetPlayerId = Request.TargetPlayerId;
    Response.MemberCount = static_cast<uint32>(Party.MemberPlayerIds.size());
    QueuePartyMemberRemovedNotify(Party, Request.TargetPlayerId, "member_kicked");
    return TResult<FPlayerKickPartyMemberResponse, FAppError>::Ok(std::move(Response));
}

TVector<SPlayerCommandParticipant> MPlayerService::BuildLogoutParticipants(uint64 PlayerId) const
{
    TVector<SPlayerCommandParticipant> Participants;
    AppendParticipant(Participants, PlayerId);

    std::lock_guard<std::mutex> Lock(SocialStateMutex);
    if (const auto TradeIdIt = PlayerTradeSessionIds.find(PlayerId);
        TradeIdIt != PlayerTradeSessionIds.end())
    {
        const auto TradeIt = TradeSessions.find(TradeIdIt->second);
        if (TradeIt != TradeSessions.end())
        {
            AppendParticipant(Participants, TradeIt->second.InitiatorPlayerId);
            AppendParticipant(Participants, TradeIt->second.TargetPlayerId);
            AppendParticipant(Participants, TradeIt->second.WitnessPlayerId);
        }
    }

    if (const auto PartyIdIt = PlayerPartyIds.find(PlayerId);
        PartyIdIt != PlayerPartyIds.end())
    {
        const auto PartyIt = Parties.find(PartyIdIt->second);
        if (PartyIt != Parties.end())
        {
            AppendParticipants(Participants, PartyIt->second.MemberPlayerIds);
            AppendParticipants(Participants, PartyIt->second.PendingInvitePlayerIds);
        }
    }

    if (const auto InviteIt = PendingPartyInviteIds.find(PlayerId);
        InviteIt != PendingPartyInviteIds.end())
    {
        const auto PartyIt = Parties.find(InviteIt->second);
        if (PartyIt != Parties.end())
        {
            AppendParticipants(Participants, PartyIt->second.MemberPlayerIds);
            AppendParticipants(Participants, PartyIt->second.PendingInvitePlayerIds);
        }
    }

    return Participants;
}

void MPlayerService::CleanupPlayerSocialState(uint64 PlayerId)
{
    TOptional<STradeSessionState> ClosedTrade;
    TOptional<SPartyState> UpdatedParty;
    TOptional<SPartyState> DisbandedParty;

    {
        std::lock_guard<std::mutex> Lock(SocialStateMutex);

        if (const auto TradeIdIt = PlayerTradeSessionIds.find(PlayerId);
            TradeIdIt != PlayerTradeSessionIds.end())
        {
            const auto TradeIt = TradeSessions.find(TradeIdIt->second);
            if (TradeIt != TradeSessions.end())
            {
                ClosedTrade = TradeIt->second;
                PlayerTradeSessionIds.erase(TradeIt->second.InitiatorPlayerId);
                PlayerTradeSessionIds.erase(TradeIt->second.TargetPlayerId);
                PlayerTradeSessionIds.erase(TradeIt->second.WitnessPlayerId);
                TradeSessions.erase(TradeIt);
            }
            else
            {
                PlayerTradeSessionIds.erase(TradeIdIt);
            }
        }

        if (const auto InviteIt = PendingPartyInviteIds.find(PlayerId);
            InviteIt != PendingPartyInviteIds.end())
        {
            const uint64 PartyId = InviteIt->second;
            PendingPartyInviteIds.erase(InviteIt);

            const auto PartyIt = Parties.find(PartyId);
            if (PartyIt != Parties.end())
            {
                ErasePlayerId(PartyIt->second.PendingInvitePlayerIds, PlayerId);
            }
        }

        if (const auto PartyIdIt = PlayerPartyIds.find(PlayerId);
            PartyIdIt != PlayerPartyIds.end())
        {
            const auto PartyIt = Parties.find(PartyIdIt->second);
            if (PartyIt == Parties.end())
            {
                PlayerPartyIds.erase(PartyIdIt);
            }
            else if (PartyIt->second.LeaderPlayerId == PlayerId)
            {
                DisbandedParty = PartyIt->second;
                for (uint64 MemberPlayerId : PartyIt->second.MemberPlayerIds)
                {
                    PlayerPartyIds.erase(MemberPlayerId);
                }
                for (uint64 InvitePlayerId : PartyIt->second.PendingInvitePlayerIds)
                {
                    PendingPartyInviteIds.erase(InvitePlayerId);
                }
                Parties.erase(PartyIt);
            }
            else
            {
                ErasePlayerId(PartyIt->second.MemberPlayerIds, PlayerId);
                PlayerPartyIds.erase(PartyIdIt);
                UpdatedParty = PartyIt->second;
            }
        }
    }

    if (ClosedTrade.has_value())
    {
        QueueTradeSessionClosedNotify(*ClosedTrade, PlayerId, "participant_logout");
    }

    if (DisbandedParty.has_value())
    {
        QueuePartyDisbandedNotify(*DisbandedParty, PlayerId, "leader_logout");
    }
    else if (UpdatedParty.has_value())
    {
        QueuePartyMemberRemovedNotify(*UpdatedParty, PlayerId, "member_logout");
    }
}
