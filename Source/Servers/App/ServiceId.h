#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Net/ServerConnection.h"  // EServerType
#include "Common/Net/Rpc/RpcManifest.h"   // GetServerTypeDisplayName

// ActorId 布局：[ServiceId: high 32][InstId: low 32]
//
// ActorId 是 64 位无符号整数；高 32 位是 ServiceId（= EServerType 数值），低 32 位是 InstId。
// 这意味着 ServiceId 实际范围 0 ~ 2^32-1；EServerType 是 uint8 存但语义值 ≤ 255。
// 把 ServiceId 用 uint32 而非 uint8 是为了：未来如果要把 ServiceId 从 EServerType 解耦
// （例如用业务类型字符串哈希作 ServiceId），位布局不用改。

namespace MServiceId
{
    constexpr uint64 ServiceIdShift = 32;
    constexpr uint64 InstIdMask     = 0x00000000FFFFFFFFull;
    constexpr uint64 ServiceIdMask  = 0xFFFFFFFF00000000ull;

    // 构造 ActorId
    inline uint64 Make(EServerType ServiceType, uint32 InstId)
    {
        return (static_cast<uint64>(static_cast<uint32>(ServiceType)) << ServiceIdShift)
             | static_cast<uint64>(InstId);
    }

    inline uint64 Make(uint32 ServiceId, uint32 InstId)
    {
        return (static_cast<uint64>(ServiceId) << ServiceIdShift)
             | static_cast<uint64>(InstId);
    }

    // 提取字段
    inline EServerType GetServiceType(uint64 ActorId)
    {
        return static_cast<EServerType>((ActorId & ServiceIdMask) >> ServiceIdShift);
    }

    inline uint32 GetServiceId(uint64 ActorId)
    {
        return static_cast<uint32>((ActorId & ServiceIdMask) >> ServiceIdShift);
    }

    inline uint32 GetInstId(uint64 ActorId)
    {
        return static_cast<uint32>(ActorId & InstIdMask);
    }

    // EServerType -> ServiceId 字符串（用于日志）。不改 EServerType 枚举本身。
    inline const char* GetServiceTypeName(EServerType Type)
    {
        return GetServerTypeDisplayName(Type);  // 复用现有 helper
    }
}