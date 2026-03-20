#pragma once

#include "Common/Runtime/MLib.h"

struct SSocketAddress
{
    MString Ip;
    uint16 Port = 0;

    SSocketAddress() = default;

    SSocketAddress(const MString& InIp, uint16 InPort)
        : Ip(InIp), Port(InPort)
    {
    }

    bool IsValid() const
    {
        return !Ip.empty() && Port != 0;
    }
};
