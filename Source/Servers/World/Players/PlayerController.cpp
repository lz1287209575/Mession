#include "Servers/World/Players/PlayerController.h"

MPlayerController::MPlayerController()
{
}

void MPlayerController::InitializeForLogin(uint32 InSceneId)
{
    SceneId = InSceneId;
    MarkPropertyDirty("SceneId");
}

void MPlayerController::SetRoute(uint32 InSceneId, uint8 InTargetServerType)
{
    SceneId = InSceneId;
    TargetServerType = InTargetServerType;
    MarkPropertyDirty("SceneId");
    MarkPropertyDirty("TargetServerType");
}
