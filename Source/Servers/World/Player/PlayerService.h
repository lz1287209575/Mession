#pragma once

#include "Servers/World/WorldServer.h"

#include "Protocol/Messages/Combat/CombatWorldMessages.h"
#include "Protocol/Messages/Scene/SceneServiceMessages.h"
#include "Protocol/Messages/World/PlayerLifecycleMessages.h"
#include "Protocol/Messages/World/PlayerModifyMessages.h"
#include "Protocol/Messages/World/PlayerMovementMessages.h"
#include "Protocol/Messages/World/PlayerQueryMessages.h"
#include "Protocol/Messages/World/PlayerRouteMessages.h"
#include "Protocol/Messages/World/PlayerSocialMessages.h"
#include "Servers/App/ObjectCallRegistry.h"
#include "Servers/World/Backend/WorldLogin.h"
#include "Servers/World/Backend/WorldMgo.h"
#include "Servers/World/Backend/WorldRouter.h"
#include "Servers/World/Backend/WorldScene.h"
#include "Servers/World/Player/Player.h"
#include "Servers/World/Player/PlayerCombatProfile.h"
#include "Servers/World/Player/PlayerCommandRuntime.h"
#include "Servers/World/Player/PlayerInventory.h"
#include "Servers/World/Player/PlayerManager.h"

#include <initializer_list>
#include <optional>

class MPlayerController;
class MPlayerCombatProfile;
class MPlayerInventory;
class MPlayerPawn;
class MPlayerProfile;
class MPlayerProgression;

class FPlayerObjectCallRootResolver final : public IObjectCallRootResolver
{
public:
    explicit FPlayerObjectCallRootResolver(const TMap<uint64, MPlayer*>* InOnlinePlayers = nullptr)
        : OnlinePlayers(InOnlinePlayers)
    {
    }

    void SetOnlinePlayers(const TMap<uint64, MPlayer*>* InOnlinePlayers)
    {
        OnlinePlayers = InOnlinePlayers;
    }

    EObjectCallRootType GetRootType() const override
    {
        return EObjectCallRootType::Player;
    }

    EServerType GetOwnerServerType() const override
    {
        return EServerType::World;
    }

    MObject* ResolveRootObject(uint64 RootId) const override
    {
        if (!OnlinePlayers)
        {
            return nullptr;
        }

        const auto It = OnlinePlayers->find(RootId);
        return It != OnlinePlayers->end() ? It->second : nullptr;
    }

private:
    const TMap<uint64, MPlayer*>* OnlinePlayers = nullptr;
};

MCLASS(Type=Service)
class MPlayerService : public MObject
{
public:
    MGENERATED_BODY(MPlayerService, MObject, 0)
public:
    void Initialize(MWorldServer* InWorldServer);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> PlayerEnterWorld(const FPlayerEnterWorldRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerFindResponse, FAppError>> PlayerFind(const FPlayerFindRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> PlayerUpdateRoute(const FPlayerUpdateRouteRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerMoveResponse, FAppError>> PlayerMove(const FPlayerMoveRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryProfileResponse, FAppError>> PlayerQueryProfile(const FPlayerQueryProfileRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryPawnResponse, FAppError>> PlayerQueryPawn(const FPlayerQueryPawnRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryInventoryResponse, FAppError>> PlayerQueryInventory(const FPlayerQueryInventoryRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryProgressionResponse, FAppError>> PlayerQueryProgression(
        const FPlayerQueryProgressionRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryCombatProfileResponse, FAppError>> PlayerQueryCombatProfile(
        const FPlayerQueryCombatProfileRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerSetPrimarySkillResponse, FAppError>> PlayerSetPrimarySkill(
        const FPlayerSetPrimarySkillRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerChangeGoldResponse, FAppError>> PlayerChangeGold(const FPlayerChangeGoldRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerEquipItemResponse, FAppError>> PlayerEquipItem(const FPlayerEquipItemRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerGrantExperienceResponse, FAppError>> PlayerGrantExperience(
        const FPlayerGrantExperienceRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> PlayerModifyHealth(
        const FPlayerModifyHealthRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerLogoutResponse, FAppError>> PlayerLogout(const FPlayerLogoutRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> PlayerSwitchScene(const FPlayerSwitchSceneRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerOpenTradeSessionResponse, FAppError>> PlayerOpenTradeSession(
        const FPlayerOpenTradeSessionRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerConfirmTradeResponse, FAppError>> PlayerConfirmTrade(
        const FPlayerConfirmTradeRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerCreatePartyResponse, FAppError>> PlayerCreateParty(
        const FPlayerCreatePartyRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerInvitePartyResponse, FAppError>> PlayerInviteParty(
        const FPlayerInvitePartyRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerAcceptPartyInviteResponse, FAppError>> PlayerAcceptPartyInvite(
        const FPlayerAcceptPartyInviteRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerKickPartyMemberResponse, FAppError>> PlayerKickPartyMember(
        const FPlayerKickPartyMemberRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FWorldCreateCombatAvatarResponse, FAppError>> CreateCombatAvatar(
        const FWorldCreateCombatAvatarRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FWorldCommitCombatResultResponse, FAppError>> CommitCombatResult(
        const FWorldCommitCombatResultRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FWorldCastSkillResponse, FAppError>> CastSkill(
        const FWorldCastSkillRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FWorldSpawnMonsterResponse, FAppError>> SpawnMonster(
        const FWorldSpawnMonsterRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FWorldCastSkillAtUnitResponse, FAppError>> CastSkillAtUnit(
        const FWorldCastSkillAtUnitRequest& Request);

    const TMap<uint64, MPlayer*>& GetOnlinePlayers() const;
    const IObjectCallRootResolver* GetCallRootResolver() const { return PlayerRootResolver.get(); }
    void FlushPersistence() const;
    void ShutdownPlayers();

private:
    enum class EDependency : uint8
    {
        Persistence,
        Login,
        Mgo,
        Scene,
        Router,
    };

    template<typename TResponse>
    std::optional<MFuture<TResult<TResponse, FAppError>>> ValidateDependencies(
        const char* FunctionName,
        std::initializer_list<EDependency> Dependencies) const;
    template<typename TResponse, typename TRequest>
    std::optional<MFuture<TResult<TResponse, FAppError>>> PrepareRuntimeDispatch(
        const TRequest& Request,
        const char* FunctionName,
        std::initializer_list<EDependency> Dependencies) const;
    static SPlayerCommandOptions BuildRuntimeCommandOptions(const char* FunctionName);
    template<typename TResponse, typename TRequest, typename TMethod>
    MFuture<TResult<TResponse, FAppError>> DispatchRuntimeCommand(
        const TRequest& Request,
        const char* FunctionName,
        std::initializer_list<EDependency> Dependencies,
        TMethod Method);
    template<typename TResponse, typename TRequest, typename TMethod>
    MFuture<TResult<TResponse, FAppError>> DispatchRuntimeCommandMany(
        const TRequest& Request,
        TVector<SPlayerCommandParticipant> Participants,
        const char* FunctionName,
        std::initializer_list<EDependency> Dependencies,
        TMethod Method);
    template<typename TTarget, typename TResponse, typename TRequest, typename TFinder, typename TMethod>
    MFuture<TResult<TResponse, FAppError>> DispatchPlayerComponent(
        const TRequest& Request,
        TFinder Finder,
        TMethod Method,
        const char* MissingCode,
        const char* FunctionName);
    template<typename TTarget, typename TResponse, typename TRequest, typename TFinder, typename TMethod>
    MFuture<TResult<TResponse, FAppError>> DispatchPlayerComponentWithSceneUpdate(
        const TRequest& Request,
        TFinder Finder,
        TMethod Method,
        const char* MissingCode,
        const char* FunctionName);

    TResult<FPlayerUpdateRouteResponse, FAppError> ApplySceneRouteForPlayer(
        uint64 PlayerId,
        uint32 SceneId) const;

    MFuture<TResult<FSceneEnterResponse, FAppError>> EnterSceneForPlayer(
        uint64 PlayerId,
        uint32 SceneId) const;

    MFuture<TResult<FSceneLeaveResponse, FAppError>> LeaveSceneForPlayer(
        uint64 PlayerId,
        uint32 CurrentSceneId) const;

    TResult<FPlayerEnterWorldResponse, FAppError> DoPlayerEnterWorld(FPlayerEnterWorldRequest Request);
    TResult<FPlayerSwitchSceneResponse, FAppError> DoPlayerSwitchScene(FPlayerSwitchSceneRequest Request);
    TResult<FPlayerLogoutResponse, FAppError> DoPlayerLogout(FPlayerLogoutRequest Request);
    TResult<FPlayerOpenTradeSessionResponse, FAppError> DoPlayerOpenTradeSession(FPlayerOpenTradeSessionRequest Request);
    TResult<FPlayerConfirmTradeResponse, FAppError> DoPlayerConfirmTrade(FPlayerConfirmTradeRequest Request);
    TResult<FPlayerCreatePartyResponse, FAppError> DoPlayerCreateParty(FPlayerCreatePartyRequest Request);
    TResult<FPlayerInvitePartyResponse, FAppError> DoPlayerInviteParty(FPlayerInvitePartyRequest Request);
    TResult<FPlayerAcceptPartyInviteResponse, FAppError> DoPlayerAcceptPartyInvite(FPlayerAcceptPartyInviteRequest Request);
    TResult<FPlayerKickPartyMemberResponse, FAppError> DoPlayerKickPartyMember(FPlayerKickPartyMemberRequest Request);
    TResult<FWorldCommitCombatResultResponse, FAppError> DoCommitCombatResult(FWorldCommitCombatResultRequest Request);
    TResult<FWorldCastSkillResponse, FAppError> DoCastSkill(FWorldCastSkillRequest Request);
    TResult<FWorldSpawnMonsterResponse, FAppError> DoSpawnMonster(FWorldSpawnMonsterRequest Request);
    TResult<FWorldCastSkillAtUnitResponse, FAppError> DoCastSkillAtUnit(FWorldCastSkillAtUnitRequest Request);

    MPlayer* FindPlayer(uint64 PlayerId) const;
    MPlayer* FindOrCreatePlayer(uint64 PlayerId);
    void RemovePlayer(uint64 PlayerId);
    MPlayerController* FindController(uint64 PlayerId) const;
    MPlayerPawn* FindPawn(uint64 PlayerId) const;
    MPlayerProfile* FindProfile(uint64 PlayerId) const;
    MPlayerInventory* FindInventory(uint64 PlayerId) const;
    MPlayerProgression* FindProgression(uint64 PlayerId) const;
    MPlayerCombatProfile* FindCombatProfile(uint64 PlayerId) const;

    MFuture<TResult<FWorldCreateCombatAvatarResponse, FAppError>> EnsureCombatAvatar(uint64 PlayerId, uint32 SceneId);
    TResult<FWorldCommitCombatResultResponse, FAppError> CommitCombatResultImmediate(
        const FWorldCommitCombatResultRequest& Request);

    struct STradeSessionState
    {
        uint64 TradeSessionId = 0;
        uint64 InitiatorPlayerId = 0;
        uint64 TargetPlayerId = 0;
        uint64 WitnessPlayerId = 0;
        bool bInitiatorConfirmed = false;
        bool bTargetConfirmed = false;
        bool bWitnessConfirmed = false;
    };

    struct SPartyState
    {
        uint64 PartyId = 0;
        uint64 LeaderPlayerId = 0;
        TVector<uint64> MemberPlayerIds;
        TVector<uint64> PendingInvitePlayerIds;
    };

    MWorldServer* WorldServer = nullptr;
    MPlayerManager* PlayerManager = nullptr;
    TUniquePtr<FPlayerObjectCallRootResolver> PlayerRootResolver;
    TUniquePtr<MPlayerCommandRuntime> PlayerCommandRuntime;
    mutable std::mutex SocialStateMutex;
    uint64 NextTradeSessionId = 1;
    uint64 NextPartyId = 1;
    TMap<uint64, STradeSessionState> TradeSessions;
    TMap<uint64, uint64> PlayerTradeSessionIds;
    TMap<uint64, SPartyState> Parties;
    TMap<uint64, uint64> PlayerPartyIds;
    TMap<uint64, uint64> PendingPartyInviteIds;

    void QueueClientNotifyToPlayer(uint64 PlayerId, uint16 FunctionId, const TByteArray& Payload) const;
    void QueueScenePlayerEnterNotify(uint64 PlayerId);
    void QueueScenePlayerUpdateNotify(uint64 PlayerId);
    void QueueScenePlayerLeaveNotify(uint64 PlayerId, uint32 SceneId);
    void QueueTradeSessionOpenedNotify(const STradeSessionState& Session);
    void QueueTradeSessionUpdatedNotify(
        const STradeSessionState& Session,
        uint64 ActorPlayerId,
        uint32 ConfirmedCount,
        bool bAllConfirmed);
    void QueueTradeSessionClosedNotify(const STradeSessionState& Session, uint64 ActorPlayerId, const char* Reason);
    void QueuePartyCreatedNotify(const SPartyState& Party);
    void QueuePartyInviteNotify(const SPartyState& Party, uint64 TargetPlayerId);
    void QueuePartyMemberJoinedNotify(const SPartyState& Party, uint64 JoinedPlayerId);
    void QueuePartyMemberRemovedNotify(const SPartyState& Party, uint64 RemovedPlayerId, const char* Reason);
    void QueuePartyDisbandedNotify(const SPartyState& Party, uint64 ActorPlayerId, const char* Reason);
    TVector<SPlayerCommandParticipant> BuildLogoutParticipants(uint64 PlayerId) const;
    void CleanupPlayerSocialState(uint64 PlayerId);
};

template<typename TResponse>
std::optional<MFuture<TResult<TResponse, FAppError>>> MPlayerService::ValidateDependencies(
    const char* FunctionName,
    std::initializer_list<EDependency> Dependencies) const
{
    for (const EDependency Dependency : Dependencies)
    {
        switch (Dependency)
        {
        case EDependency::Persistence:
            break;
        case EDependency::Login:
            if (!WorldServer->GetLogin() || !WorldServer->GetLogin()->IsAvailable())
            {
                return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                    "login_server_unavailable",
                    FunctionName ? FunctionName : "");
            }
            break;
        case EDependency::Mgo:
            if (!WorldServer->GetMgo() || !WorldServer->GetMgo()->IsAvailable())
            {
                return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                    "mgo_server_unavailable",
                    FunctionName ? FunctionName : "");
            }
            break;
        case EDependency::Scene:
            if (!WorldServer->GetScene() || !WorldServer->GetScene()->IsAvailable())
            {
                return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                    "scene_server_unavailable",
                    FunctionName ? FunctionName : "");
            }
            break;
        case EDependency::Router:
            if (!WorldServer->GetRouter() || !WorldServer->GetRouter()->IsAvailable())
            {
                return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                    "router_server_unavailable",
                    FunctionName ? FunctionName : "");
            }
            break;
        }
    }

    return std::nullopt;
}

template<typename TResponse, typename TRequest>
std::optional<MFuture<TResult<TResponse, FAppError>>> MPlayerService::PrepareRuntimeDispatch(
    const TRequest& Request,
    const char* FunctionName,
    std::initializer_list<EDependency> Dependencies) const
{
    if (const TOptional<FAppError> ValidationError = MServerCallRequestValidation::ValidateRequest(Request);
        ValidationError.has_value())
    {
        return MServerCallAsyncSupport::MakeResultFuture(MakeErrorResult<TResponse>(*ValidationError));
    }

    if (auto Error = ValidateDependencies<TResponse>(FunctionName, Dependencies); Error.has_value())
    {
        return std::move(*Error);
    }

    if (!PlayerCommandRuntime)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
            "player_command_runtime_missing",
            FunctionName ? FunctionName : "");
    }

    return std::nullopt;
}

inline SPlayerCommandOptions MPlayerService::BuildRuntimeCommandOptions(const char* FunctionName)
{
    return SPlayerCommandOptions{
        FunctionName ? FunctionName : "",
        0,
        true,
    };
}

template<typename TTarget, typename TResponse, typename TRequest, typename TFinder, typename TMethod>
inline MFuture<TResult<TResponse, FAppError>> MPlayerService::DispatchPlayerComponent(
    const TRequest& Request,
    TFinder Finder,
    TMethod Method,
    const char* MissingCode,
    const char* FunctionName)
{
    TTarget* Target = (this->*Finder)(Request.PlayerId);
    if (!Target)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(MissingCode, FunctionName);
    }

    return (Target->*Method)(Request);
}

template<typename TTarget, typename TResponse, typename TRequest, typename TFinder, typename TMethod>
inline MFuture<TResult<TResponse, FAppError>> MPlayerService::DispatchPlayerComponentWithSceneUpdate(
    const TRequest& Request,
    TFinder Finder,
    TMethod Method,
    const char* MissingCode,
    const char* FunctionName)
{
    return MServerCallAsyncSupport::TapSuccess(
        DispatchPlayerComponent<TTarget, TResponse>(Request, Finder, Method, MissingCode, FunctionName),
        [this, PlayerId = Request.PlayerId](const TResponse&)
        {
            QueueScenePlayerUpdateNotify(PlayerId);
        });
}

template<typename TResponse, typename TRequest, typename TMethod>
inline MFuture<TResult<TResponse, FAppError>> MPlayerService::DispatchRuntimeCommand(
    const TRequest& Request,
    const char* FunctionName,
    std::initializer_list<EDependency> Dependencies,
    TMethod Method)
{
    if (auto Error = PrepareRuntimeDispatch<TResponse>(Request, FunctionName, Dependencies); Error.has_value())
    {
        return std::move(*Error);
    }

    return PlayerCommandRuntime->Enqueue(
        Request.PlayerId,
        BuildRuntimeCommandOptions(FunctionName),
        this,
        Method,
        TRequest(Request));
}

template<typename TResponse, typename TRequest, typename TMethod>
inline MFuture<TResult<TResponse, FAppError>> MPlayerService::DispatchRuntimeCommandMany(
    const TRequest& Request,
    TVector<SPlayerCommandParticipant> Participants,
    const char* FunctionName,
    std::initializer_list<EDependency> Dependencies,
    TMethod Method)
{
    if (auto Error = PrepareRuntimeDispatch<TResponse>(Request, FunctionName, Dependencies); Error.has_value())
    {
        return std::move(*Error);
    }

    return PlayerCommandRuntime->EnqueueMany(
        std::move(Participants),
        BuildRuntimeCommandOptions(FunctionName),
        this,
        Method,
        TRequest(Request));
}
