#pragma once

#include "Core/Net/NetCore.h"

struct SSocketAddress
{
    FString Ip;
    uint16 Port = 0;

    SSocketAddress() = default;

    SSocketAddress(const FString& InIp, uint16 InPort)
        : Ip(InIp), Port(InPort)
    {
    }

    bool IsValid() const
    {
        return !Ip.empty() && Port != 0;
    }
};
