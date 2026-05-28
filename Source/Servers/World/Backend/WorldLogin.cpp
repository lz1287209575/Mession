#include "Servers/World/Backend/WorldLogin.h"
#include "Common/Runtime/Log/Logger.h"

EServerType MWorldLogin::GetTargetServerType() const
{
    return EServerType::Login;
}
