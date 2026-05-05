#pragma once
#include "Protocol/Messages/World/PlayerLifecycleMessages.h"

template<typename TResponse>
TResponse BuildPlayerOnlyResponse(uint64 PlayerId)
{
    TResponse Response;
    Response.PlayerId = PlayerId;
    return Response;
}
