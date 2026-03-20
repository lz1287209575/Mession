#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Net/ServerConnection.h"

struct SBackendPeer
{
    TSharedPtr<INetConnection> Connection;
    bool bAuthenticated = false;
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    MString ServerName;
};
