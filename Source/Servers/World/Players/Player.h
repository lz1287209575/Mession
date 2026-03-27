#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/World/WorldPlayerMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/World/Players/PlayerController.h"
#include "Servers/World/Players/PlayerPawn.h"
#include "Servers/World/Players/PlayerProfile.h"
#include "Servers/World/Players/PlayerSession.h"

MCLASS(Type=Object)
class MPlayer : public MObject
{
public:
    MGENERATED_BODY(MPlayer, MObject, 0)
public:
    MPlayer();

    MPROPERTY(Replicated)
    uint64 PlayerId = 0;

    void InitializeForLogin(uint64 InPlayerId, uint64 InGatewayConnectionId, uint32 InSessionKey);

    void SetRoute(uint32 InSceneId, uint8 InTargetServerType);

    void FinalizeLoadedState();

    uint32 ResolveCurrentSceneId() const;

    uint32 ResolveCurrentHealth() const;

    void SyncRuntimeStateToProfile();

    void PrepareForLogout();

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerFindResponse, FAppError>> PlayerFind(const FPlayerFindRequest& Request);

    MPlayerSession* GetSession() const;

    MPlayerController* GetController() const;

    MPlayerPawn* GetPawn() const;

    MPlayerProfile* GetProfile() const;

    void VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const override;

private:
    MPlayerSession* Session = nullptr;
    MPlayerController* Controller = nullptr;
    MPlayerPawn* Pawn = nullptr;
    MPlayerProfile* Profile = nullptr;
};
