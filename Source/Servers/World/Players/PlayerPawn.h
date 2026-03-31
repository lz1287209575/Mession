#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/World/WorldPlayerMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

MCLASS(Type=Object)
class MPlayerPawn : public MObject
{
public:
    MGENERATED_BODY(MPlayerPawn, MObject, 0)
public:
    MPlayerPawn();

    // Runtime world-presence state. Persistence still flows through Profile/Progression for compatibility.
    MPROPERTY(Replicated)
    uint32 SceneId = 0;

    MPROPERTY(Replicated)
    float X = 0.0f;

    MPROPERTY(Replicated)
    float Y = 0.0f;

    MPROPERTY(Replicated)
    float Z = 0.0f;

    MPROPERTY(Replicated)
    uint32 Health = 100;

    void InitializeForLogin(uint32 InSceneId, uint32 InHealth, float InX = 0.0f, float InY = 0.0f, float InZ = 0.0f);

    void SyncFromPersistence(uint32 InSceneId, uint32 InHealth, float InX = 0.0f, float InY = 0.0f, float InZ = 0.0f);

    void Spawn(uint32 InSceneId, uint32 InHealth, float InX = 0.0f, float InY = 0.0f, float InZ = 0.0f);

    void Despawn();

    void SetSceneId(uint32 InSceneId);

    void SetLocation(float InX, float InY, float InZ);

    void SetHealth(uint32 InHealth);

    bool IsSpawned() const;

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryPawnResponse, FAppError>> PlayerQueryPawn(const FPlayerQueryPawnRequest& Request);
};
