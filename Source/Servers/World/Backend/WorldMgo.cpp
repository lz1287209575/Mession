#include "Servers/World/Backend/WorldMgo.h"
#include "Common/Runtime/Log/Logger.h"

EServerType MWorldMgo::GetTargetServerType() const
{
    return EServerType::Mgo;
}
