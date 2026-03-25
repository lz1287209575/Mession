#include "Servers/World/Players/PlayerPawn.h"

MPlayerPawn::MPlayerPawn()
{
}

void MPlayerPawn::InitializeForLogin(uint32 InSceneId, uint32 InHealth)
{
    SceneId = InSceneId;
    Health = InHealth;
    MarkPropertyDirty("SceneId");
    MarkPropertyDirty("Health");
}

void MPlayerPawn::SyncFromPersistence(uint32 InSceneId, uint32 InHealth)
{
    SceneId = InSceneId;
    Health = InHealth;
}

void MPlayerPawn::SetSceneId(uint32 InSceneId)
{
    SceneId = InSceneId;
    MarkPropertyDirty("SceneId");
}

void MPlayerPawn::SetHealth(uint32 InHealth)
{
    Health = InHealth;
    MarkPropertyDirty("Health");
}
