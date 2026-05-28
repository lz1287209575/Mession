#include "Servers/Gateway/Rpc/GatewayBackendRpc.h"
#include "Common/Runtime/Log/Logger.h"

EServerType MGatewayWorld::GetTargetServerType() const
{
    return EServerType::World;
}
