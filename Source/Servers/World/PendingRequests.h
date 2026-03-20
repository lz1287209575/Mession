#pragma once

#include "Common/Runtime/MLib.h"

struct SPendingSessionValidation
{
    uint64 ValidationRequestId = 0;
    uint64 GatewayConnectionId = 0;
    uint64 PlayerId = 0;
    uint32 SessionKey = 0;
};

struct SPendingMgoLoad
{
    uint64 RequestId = 0;
    uint64 GatewayConnectionId = 0;
    uint64 PlayerId = 0;
    uint32 SessionKey = 0;
};

struct SPendingMgoPersist
{
    uint64 RequestId = 0;
    uint64 ObjectId = 0;
    uint64 Version = 0;
    double DispatchTime = 0.0;
};
