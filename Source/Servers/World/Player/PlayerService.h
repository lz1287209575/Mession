#pragma once

#include "Servers/World/WorldServer.h"

#include "Protocol/Messages/Combat/CombatWorldMessages.h"
#include "Protocol/Messages/Scene/SceneServiceMessages.h"
#include "Protocol/Messages/World/PlayerLifecycleMessages.h"
#include "Protocol/Messages/World/PlayerModifyMessages.h"
#include "Protocol/Messages/World/PlayerMovementMessages.h"
#include "Protocol/Messages/World/PlayerQueryMessages.h"
#include "Protocol/Messages/World/PlayerRouteMessages.h"
#include "Servers/App/ObjectCallRegistry.h"
#include "Servers/World/Backend/WorldLogin.h"
#include "Servers/World/Backend/WorldMgo.h"
#include "Servers/World/Backend/WorldRouter.h"
#include "Servers/World/Backend/WorldScene.h"
#include "Servers/World/Player/Player.h"

#include <initializer_list>
#include <optional>

namespace MPlayerActions
{
class FPlayerEnterAction;
class FPlayerLogoutAction;
class FPlayerSwitchSceneAction;
}

class MPlayerController;
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
    MFuture<TResult<FWorldCreateCombatAvatarResponse, FAppError>> CreateCombatAvatar(
        const FWorldCreateCombatAvatarRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FWorldCommitCombatResultResponse, FAppError>> CommitCombatResult(
        const FWorldCommitCombatResultRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FWorldCastSkillResponse, FAppError>> CastSkill(
        const FWorldCastSkillRequest& Request);

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

    friend class MPlayerActions::FPlayerEnterAction;
    friend class MPlayerActions::FPlayerLogoutAction;
    friend class MPlayerActions::FPlayerSwitchSceneAction;

    template<typename TResponse>
    std::optional<MFuture<TResult<TResponse, FAppError>>> ValidateDependencies(
        const char* FunctionName,
        std::initializer_list<EDependency> Dependencies) const;

    MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> ApplySceneRouteForPlayer(
        uint64 PlayerId,
        uint32 SceneId) const;

    MFuture<TResult<FSceneEnterResponse, FAppError>> EnterSceneForPlayer(
        uint64 PlayerId,
        uint32 SceneId) const;

    MFuture<TResult<FSceneLeaveResponse, FAppError>> LeaveSceneForPlayer(
        uint64 PlayerId,
        uint32 CurrentSceneId) const;

    MPlayer* FindPlayer(uint64 PlayerId) const;
    MPlayer* FindOrCreatePlayer(uint64 PlayerId);
    void RemovePlayer(uint64 PlayerId);
    MPlayerController* FindController(uint64 PlayerId) const;
    MPlayerPawn* FindPawn(uint64 PlayerId) const;
    MPlayerProfile* FindProfile(uint64 PlayerId) const;
    MPlayerInventory* FindInventory(uint64 PlayerId) const;
    MPlayerProgression* FindProgression(uint64 PlayerId) const;

    void QueueScenePlayerEnterNotify(uint64 PlayerId);
    void QueueScenePlayerUpdateNotify(uint64 PlayerId);
    void QueueScenePlayerLeaveNotify(uint64 PlayerId, uint32 SceneId);

    MFuture<TResult<FWorldCreateCombatAvatarResponse, FAppError>> EnsureCombatAvatar(uint64 PlayerId, uint32 SceneId);

    MWorldServer* WorldServer = nullptr;
    TMap<uint64, MPlayer*> OnlinePlayers;
    TUniquePtr<FPlayerObjectCallRootResolver> PlayerRootResolver;
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

namespace MPlayerServiceDetail
{
template<typename TTarget, typename TResponse, typename TRequest, typename TFinder, typename TMethod>
inline MFuture<TResult<TResponse, FAppError>> DispatchPlayerComponent(
    MPlayerService* Service,
    const TRequest& Request,
    TFinder Finder,
    TMethod Method,
    const char* MissingCode,
    const char* FunctionName)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>("player_id_required", FunctionName);
    }

    TTarget* Target = (Service->*Finder)(Request.PlayerId);
    if (!Target)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(MissingCode, FunctionName);
    }

    return (Target->*Method)(Request);
}
} // namespace MPlayerServiceDetail
