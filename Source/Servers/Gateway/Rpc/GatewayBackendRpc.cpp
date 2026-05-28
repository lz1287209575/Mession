#include "Servers/Gateway/Rpc/GatewayBackendRpc.h"
#include "Common/Runtime/Log/Logger.h"

EServerType MGatewayLogin::GetTargetServerType() const
{
    return EServerType::Login;
}
