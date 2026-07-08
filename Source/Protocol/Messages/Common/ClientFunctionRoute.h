#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Net/ServerConnection.h"

struct FClientFunctionRoute
{
    uint16 ClientFunctionId = 0;
    EServerType TargetServiceType = EServerType::Unknown;
    const char* ClassName = nullptr;
    const char* MethodName = nullptr;
    bool bRequiresActorId = false;
};

namespace MClientFunctionRouteTable
{
inline const TVector<FClientFunctionRoute>& GetRoutes()
{
    static const TVector<FClientFunctionRoute> Routes = {
        // PoC 第 1 步：仅 Echo 一条；第 2 步再加 SampleB 路由
        { 8801, EServerType::Echo, "MEchoService", "Echo", /*bRequiresActorId=*/true },
    };
    return Routes;
}

inline const FClientFunctionRoute* FindRoute(uint16 ClientFunctionId)
{
    for (const auto& Route : GetRoutes())
    {
        if (Route.ClientFunctionId == ClientFunctionId)
        {
            return &Route;
        }
    }
    return nullptr;
}
}