#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"

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
    uint32 Health = 100;

    void InitializeForLogin(uint32 InSceneId, uint32 InHealth);

    void SyncFromPersistence(uint32 InSceneId, uint32 InHealth);

    void Spawn(uint32 InSceneId, uint32 InHealth);

    void Despawn();

    void SetSceneId(uint32 InSceneId);

    void SetHealth(uint32 InHealth);

    bool IsSpawned() const;
};
