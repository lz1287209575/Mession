#pragma once

#include "Common/MLib.h"
#include <cstring>

enum class EPacketDecodeResult : uint8
{
    NoPacket = 0,
    PacketReady = 1,
    InvalidPacket = 2
};

class MLengthPrefixedPacketCodec
{
public:
    static bool EncodePacket(const TByteArray& Payload, TByteArray& OutBytes)
    {
        if (Payload.empty() || Payload.size() > MAX_PACKET_SIZE)
        {
            return false;
        }

        const uint32 PacketSize = static_cast<uint32>(Payload.size());
        OutBytes.resize(sizeof(PacketSize) + PacketSize);
        memcpy(OutBytes.data(), &PacketSize, sizeof(PacketSize));
        memcpy(OutBytes.data() + sizeof(PacketSize), Payload.data(), PacketSize);
        return true;
    }

    static EPacketDecodeResult TryDecodePacket(TByteArray& InOutRecvBuffer, TByteArray& OutPayload)
    {
        OutPayload.clear();

        if (InOutRecvBuffer.size() < sizeof(uint32))
        {
            return EPacketDecodeResult::NoPacket;
        }

        uint32 PacketSize = 0;
        memcpy(&PacketSize, InOutRecvBuffer.data(), sizeof(PacketSize));

        if (PacketSize == 0 || PacketSize > MAX_PACKET_SIZE)
        {
            return EPacketDecodeResult::InvalidPacket;
        }

        const size_t TotalSize = sizeof(PacketSize) + PacketSize;
        if (InOutRecvBuffer.size() < TotalSize)
        {
            return EPacketDecodeResult::NoPacket;
        }

        OutPayload.assign(
            InOutRecvBuffer.begin() + sizeof(PacketSize),
            InOutRecvBuffer.begin() + TotalSize);
        InOutRecvBuffer.erase(InOutRecvBuffer.begin(), InOutRecvBuffer.begin() + TotalSize);
        return EPacketDecodeResult::PacketReady;
    }
};
