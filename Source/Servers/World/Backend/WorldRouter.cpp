#include "Servers/World/Backend/WorldRouter.h"
#include "Common/Runtime/Log/Logger.h"

EServerType MWorldRouter::GetTargetServerType() const
{
    return EServerType::Router;
}
