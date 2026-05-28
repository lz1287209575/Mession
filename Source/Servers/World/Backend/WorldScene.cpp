#include "Servers/World/Backend/WorldScene.h"
#include "Common/Runtime/Log/Logger.h"

EServerType MWorldScene::GetTargetServerType() const
{
    return EServerType::Scene;
}
