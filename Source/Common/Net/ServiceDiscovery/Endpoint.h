#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Net/ServerConnection.h"  // EServerType
#include "Common/Runtime/Reflect/Reflection.h"
#include "Servers/App/ServiceId.h"

MSTRUCT()
struct FServiceEndpoint
{
    MPROPERTY() EServerType ServerType = EServerType::Unknown;
    MPROPERTY() uint32 ServerId = 0;
    MPROPERTY() MString Address = "0.0.0.0";
    MPROPERTY() uint16 Port = 0;
    MPROPERTY() uint64 LastHeartbeatMs = 0;
    MPROPERTY() bool bHealthy = true;
    MPROPERTY() TVector<uint64> ActorIds;
};

// Wire-level enum for the Registry protocol. Lives outside EServerMessageType /
// EClientMessageType so we can hand MServerConnection the raw uint8 PacketType
// without colliding with MT_RPC=27 / MT_FunctionCall=28 / MT_FunctionResponse=29
// (see Common/Net/ServerConnection.h:26-31). Base 200 is well clear of those.
enum class EServiceRegistryMessageType : uint8_t
{
    Register       = 200,
    Deregister     = 201,
    Heartbeat      = 202,
    UpdateActors   = 203,
    ListEndpoints  = 204,
    EndpointChange = 205,
    Ack            = 206,
};

enum class EServiceRegistryResult : uint8_t
{
    Ok             = 0,
    AlreadyExists  = 1,
    NotFound       = 2,
    InvalidPayload = 3,
    StaleSeq       = 4,
};

bool EncodeEndpoint(const FServiceEndpoint& Ep, TByteArray& Out);
bool DecodeEndpoint(const TByteArray& In, size_t& Off, FServiceEndpoint& Ep);

/**
 * MakeLocalEndpoint - 把 Service config 转成 FServiceEndpoint（Registry 注册用）。
 *
 * PoC 阶段 Address="0.0.0.0"——Registry 不解析 IP，由 Service 自己在 LazyConnect
 * 时用 loopback + Config.ListenPort 通；跨主机写在 follow-up spec。
 *
 * LocalActorIds 是 InstId 列表（CLI 输入形式）→ 在这里转成 64-bit ActorId
 * （高 32 位 = ServiceId = EServerType 数值）。
 */
template<typename TConfig>
FServiceEndpoint MakeLocalEndpoint(const TConfig& Config)
{
    FServiceEndpoint Ep;
    Ep.ServerType = Config.LocalServerType;
    Ep.ServerId = Config.LocalServerId;
    Ep.Address = "0.0.0.0";
    Ep.Port = Config.ListenPort;
    Ep.ActorIds.reserve(Config.LocalActorIds.size());
    for (uint32 InstId : Config.LocalActorIds)
    {
        Ep.ActorIds.push_back(MServiceId::Make(Config.LocalServerType, InstId));
    }
    return Ep;
}

/**
 * ParseAddrPort - 把 "host:port" 拆成 host + port。返回 false 表示格式错误。
 *
 * 给 BindRegistry 用：CLI --registry=127.0.0.1:18000 → 这里拆出来。
 */
inline bool ParseAddrPort(const MString& In, MString& OutAddr, uint16& OutPort)
{
    const size_t ColonPos = In.rfind(':');
    if (ColonPos == MString::npos || ColonPos == 0 || ColonPos + 1 >= In.size())
    {
        return false;
    }
    const MString PortStr = In.substr(ColonPos + 1);
    int PortInt = 0;
    try
    {
        PortInt = std::stoi(PortStr.c_str());
    }
    catch (const std::exception&)
    {
        return false;
    }
    if (PortInt <= 0 || PortInt > 65535)
    {
        return false;
    }
    OutAddr = In.substr(0, ColonPos);
    OutPort = static_cast<uint16>(PortInt);
    return true;
}