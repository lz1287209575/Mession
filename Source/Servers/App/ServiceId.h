/**
 * @file ServiceId.h
 * @brief MServiceId —— ActorId 的 64 位打包 / 拆解工具。
 *
 * @detail ActorId = (ServiceId << 32) | InstId,全进程统一同一套位布局,因此跨进程路由
 *   时不需要额外字段就能从 ActorId 反推出目标服务类型与实例号。
 *   本文件只做纯位运算:无状态、无锁、不分配内存,可从任意线程调用。
 */
#pragma once

#include "Common/Net/Rpc/RpcManifest.h"  // 提供 GetServerTypeDisplayName
#include "Common/Net/ServerConnection.h" // 提供 EServerType
#include "Common/Runtime/MLib.h"

// ActorId 布局:[ServiceId: 高 32 位][InstId: 低 32 位]
//
// ActorId 是 64 位无符号整数:高 32 位存 ServiceId,低 32 位存 InstId。
// ServiceId 的语义值就是 EServerType 的数值(EServerType 是 `enum class EServerType : uint32`),
// 有效范围 0 ~ 2^32-1。这里刻意用 uint32 承载 ServiceId,而不是收窄到枚举自身的宽度:
// 将来若把 ServiceId 与 EServerType 解耦(例如改用业务类型字符串哈希作 ServiceId),
// 位布局不需要跟着改。

namespace MServiceId {
    constexpr uint64 ServiceIdShift = 32;
    constexpr uint64 InstIdMask     = 0x00000000FFFFFFFFull;
    constexpr uint64 ServiceIdMask  = 0xFFFFFFFF00000000ull;

    /** @brief Make - 由服务类型 + 实例号构造 ActorId(ServiceType 放高位)。 */
    inline uint64 Make(EServerType ServiceType, uint32 InstId) {
        return (static_cast<uint64>(static_cast<uint32>(ServiceType)) << ServiceIdShift) | static_cast<uint64>(InstId);
    }

    /** @brief Make - 由裸 ServiceId + 实例号构造 ActorId(不校验 ServiceId 的合法性)。 */
    inline uint64 Make(uint32 ServiceId, uint32 InstId) {
        return (static_cast<uint64>(ServiceId) << ServiceIdShift) | static_cast<uint64>(InstId);
    }

    /** @brief GetServiceType - 取出高位 ServiceId 并转成 EServerType(不校验取值是否在枚举内)。 */
    inline EServerType GetServiceType(uint64 ActorId) {
        return static_cast<EServerType>((ActorId & ServiceIdMask) >> ServiceIdShift);
    }

    /** @brief GetServiceId - 取出高位 ServiceId(原始 uint32,不做枚举转换)。 */
    inline uint32 GetServiceId(uint64 ActorId) {
        return static_cast<uint32>((ActorId & ServiceIdMask) >> ServiceIdShift);
    }

    /** @brief GetInstId - 取出低位 InstId。 */
    inline uint32 GetInstId(uint64 ActorId) {
        return static_cast<uint32>(ActorId & InstIdMask);
    }

    /**
     * @brief GetServiceTypeName - EServerType 转显示名,供日志使用。
     * @param Type 服务类型
     * @return 静态字符串,生命周期贯穿进程;未知类型返回 Unknown 的展示名
     */
    inline const char* GetServiceTypeName(EServerType Type) {
        return GetServerTypeDisplayName(Type); // 复用现有 helper
    }
} // namespace MServiceId