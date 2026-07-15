#include "Servers/ServiceRegistry/ServiceRegistryServer.h"

#include "Servers/App/MService.h"
#include "Common/Net/Rpc/RpcManifest.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/Time.h"

bool MServiceRegistry::Init(int InPort)
{
    Config = MService<SServiceRegistryConfig>::GetConfig();
    if (InPort > 0) Config.ListenPort = static_cast<uint16>(InPort);
    if (Config.ListenPort == 0)
    {
        LOG_ERROR("MServiceRegistry: ListenPort is 0");
        return false;
    }

    bRunning = true;
    MLogger::LogStartupBanner("MServiceRegistry", Config.ListenPort, 0);
    return true;
}

uint16 MServiceRegistry::GetListenPort() const
{
    return Config.ListenPort;
}

void MServiceRegistry::OnRunStarted()
{
    LOG_INFO("MServiceRegistry running on port %u", static_cast<unsigned>(Config.ListenPort));
}

void MServiceRegistry::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    auto Session = MakeShared<FRegistryClientSession>();
    Session->ConnId = ConnId;
    Session->Conn = Conn;
    Session->LastSeenMs = static_cast<uint64>(MTime::GetTimeSeconds() * 1000);
    Sessions_[ConnId] = Session;

    EventLoop.RegisterConnection(
        ConnId,
        Conn,
        [this, ConnId](uint64 /*ConnectionId*/, const TByteArray& Packet)
        {
            auto It = Sessions_.find(ConnId);
            if (It == Sessions_.end()) return;
            HandlePacket(It->second, Packet);
        },
        [this, ConnId](uint64 ConnectionId)
        {
            auto It = Sessions_.find(ConnectionId);
            if (It == Sessions_.end()) return;
            if (It->second->bRegistered)
            {
                const uint32 ServerId = It->second->ServerId;
                auto EpIt = Endpoints_.find(ServerId);
                if (EpIt != Endpoints_.end())
                {
                    const EServerType ServerType = EpIt->second.ServerType;
                    Endpoints_.erase(EpIt);
                    SendEndpointChange(ServerType);
                    LOG_INFO("MServiceRegistry: server %u disconnected, removed endpoint",
                             static_cast<unsigned>(ServerId));
                }
            }
            Sessions_.erase(ConnectionId);
        });
}

void MServiceRegistry::HandlePacket(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Packet)
{
    if (Packet.empty()) return;
    Session->LastSeenMs = static_cast<uint64>(MTime::GetTimeSeconds() * 1000);
    const uint8 PacketType = Packet[0];
    const TByteArray Payload(Packet.begin() + 1, Packet.end());

    switch (static_cast<EServiceRegistryMessageType>(PacketType))
    {
    case EServiceRegistryMessageType::Register:
        HandleRegister(Session, Payload);
        break;
    case EServiceRegistryMessageType::Deregister:
        HandleDeregister(Session, Payload);
        break;
    case EServiceRegistryMessageType::Heartbeat:
        HandleHeartbeat(Session, Payload);
        break;
    case EServiceRegistryMessageType::UpdateActors:
        HandleUpdateActors(Session, Payload);
        break;
    case EServiceRegistryMessageType::ListEndpoints:
        HandleListEndpoints(Session, Payload);
        break;
    default:
        LOG_WARN("MServiceRegistry: unknown packet type=%u from conn=%llu",
                 static_cast<unsigned>(PacketType),
                 static_cast<unsigned long long>(Session->ConnId));
        break;
    }
}

void MServiceRegistry::HandleRegister(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload)
{
    FServiceEndpoint Ep;
    if (!RegistryProtocol::ParseRegistryRegisterPacket(Payload, Ep))
    {
        SendAck(Session, EServiceRegistryResult::InvalidPayload, "register_parse_failed");
        return;
    }

    auto Existing = Endpoints_.find(Ep.ServerId);
    if (Existing != Endpoints_.end() && !Session->bRegistered)
    {
        SendAck(Session, EServiceRegistryResult::AlreadyExists, "server_id_in_use");
        if (MLogger::IsDebugBuild())
        {
            LOG_FATAL("MServiceRegistry: duplicate ServerId=%u (DEBUG abort)",
                      static_cast<unsigned>(Ep.ServerId));
        }
        else
        {
            LOG_ERROR("MServiceRegistry: rejected duplicate ServerId=%u",
                      static_cast<unsigned>(Ep.ServerId));
        }
        return;
    }

    Ep.LastHeartbeatMs = static_cast<uint64>(MTime::GetTimeSeconds() * 1000);
    Ep.bHealthy = true;
    const EServerType ChangedType = Ep.ServerType;
    Endpoints_[Ep.ServerId] = Ep;
    Session->ServerId = Ep.ServerId;
    Session->bRegistered = true;

    SendAck(Session, EServiceRegistryResult::Ok, "");
    SendEndpointChange(ChangedType);
    LOG_INFO("MServiceRegistry: registered ServerId=%u Type=%s Address=%s:%u",
             static_cast<unsigned>(Ep.ServerId),
             GetServerTypeDisplayName(Ep.ServerType),
             Ep.Address.c_str(),
             static_cast<unsigned>(Ep.Port));
}

void MServiceRegistry::HandleDeregister(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload)
{
    uint32 ServerId = 0;
    if (!RegistryProtocol::ParseRegistryDeregisterPacket(Payload, ServerId))
    {
        SendAck(Session, EServiceRegistryResult::InvalidPayload, "deregister_parse_failed");
        return;
    }
    auto It = Endpoints_.find(ServerId);
    if (It == Endpoints_.end())
    {
        SendAck(Session, EServiceRegistryResult::NotFound, "server_id_unknown");
        return;
    }
    const EServerType ChangedType = It->second.ServerType;
    Endpoints_.erase(It);
    Session->bRegistered = false;
    SendAck(Session, EServiceRegistryResult::Ok, "");
    SendEndpointChange(ChangedType);
}

void MServiceRegistry::HandleHeartbeat(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload)
{
    uint32 ServerId = 0;
    uint64 TimestampMs = 0;
    if (!RegistryProtocol::ParseRegistryHeartbeatPacket(Payload, ServerId, TimestampMs))
    {
        SendAck(Session, EServiceRegistryResult::InvalidPayload, "heartbeat_parse_failed");
        return;
    }
    if (ServerId != Session->ServerId)
    {
        SendAck(Session, EServiceRegistryResult::NotFound, "server_id_mismatch");
        return;
    }
    auto It = Endpoints_.find(ServerId);
    if (It != Endpoints_.end())
    {
        It->second.LastHeartbeatMs = static_cast<uint64>(MTime::GetTimeSeconds() * 1000);
        if (!It->second.bHealthy)
        {
            It->second.bHealthy = true;
            SendEndpointChange(It->second.ServerType);
        }
    }
}

void MServiceRegistry::HandleUpdateActors(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload)
{
    uint32 ServerId = 0;
    TVector<uint64> ActorIds;
    if (!RegistryProtocol::ParseRegistryUpdateActorsPacket(Payload, ServerId, ActorIds))
    {
        SendAck(Session, EServiceRegistryResult::InvalidPayload, "update_actors_parse_failed");
        return;
    }
    auto It = Endpoints_.find(ServerId);
    if (It == Endpoints_.end())
    {
        SendAck(Session, EServiceRegistryResult::NotFound, "server_id_unknown");
        return;
    }
    It->second.ActorIds = ActorIds;
    SendEndpointChange(It->second.ServerType);
}

void MServiceRegistry::HandleListEndpoints(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload)
{
    EServerType Type = EServerType::Unknown;
    if (!RegistryProtocol::ParseRegistryListEndpointsPacket(Payload, Type))
    {
        SendAck(Session, EServiceRegistryResult::InvalidPayload, "list_parse_failed");
        return;
    }
    TVector<FServiceEndpoint> Filtered;
    for (const auto& Pair : Endpoints_)
    {
        if (Pair.second.ServerType == Type && Pair.second.bHealthy)
        {
            Filtered.push_back(Pair.second);
        }
    }
    uint64& Seq = MonotonicSeq_[Type];
    Seq += 1;
    TByteArray Packet;
    RegistryProtocol::BuildRegistryEndpointChangePacket(Type, Seq, Filtered, Packet);
    SendTo(Session, Packet);
}

void MServiceRegistry::SendTo(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Packet)
{
    if (!Session || !Session->Conn || !Session->Conn->IsConnected()) return;
    Session->Conn->Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

void MServiceRegistry::SendAck(TSharedPtr<FRegistryClientSession> Session, EServiceRegistryResult Status, const MString& Message)
{
    TByteArray Packet;
    RegistryProtocol::BuildRegistryAckPacket(Status, Message, Packet);
    SendTo(Session, Packet);
}

void MServiceRegistry::SendEndpointChange(EServerType ServerType)
{
    TVector<FServiceEndpoint> Filtered;
    for (const auto& Pair : Endpoints_)
    {
        if (Pair.second.ServerType == ServerType)
        {
            Filtered.push_back(Pair.second);
        }
    }
    uint64& Seq = MonotonicSeq_[ServerType];
    Seq += 1;
    TByteArray Packet;
    RegistryProtocol::BuildRegistryEndpointChangePacket(ServerType, Seq, Filtered, Packet);
    for (auto& [ConnId, Session] : Sessions_)
    {
        SendTo(Session, Packet);
    }
}

void MServiceRegistry::TickHeartbeats()
{
    const uint64 NowMs = static_cast<uint64>(MTime::GetTimeSeconds() * 1000);
    TVector<EServerType> ChangedTypes;
    for (auto It = Endpoints_.begin(); It != Endpoints_.end();)
    {
        FServiceEndpoint& Ep = It->second;
        const uint64 Age = NowMs - Ep.LastHeartbeatMs;
        if (Age > static_cast<uint64>(Config.EvictAfterMs))
        {
            const EServerType Type = Ep.ServerType;
            LOG_INFO("MServiceRegistry: evicting ServerId=%u (silent for %llu ms)",
                     static_cast<unsigned>(Ep.ServerId),
                     static_cast<unsigned long long>(Age));
            It = Endpoints_.erase(It);
            ChangedTypes.push_back(Type);
        }
        else if (Age > static_cast<uint64>(Config.HeartbeatTimeoutMs) && Ep.bHealthy)
        {
            Ep.bHealthy = false;
            LOG_INFO("MServiceRegistry: ServerId=%u marked unhealthy (silent for %llu ms)",
                     static_cast<unsigned>(Ep.ServerId),
                     static_cast<unsigned long long>(Age));
            ChangedTypes.push_back(Ep.ServerType);
        }
        else
        {
            ++It;
        }
    }
    for (EServerType T : ChangedTypes)
    {
        SendEndpointChange(T);
    }
}

void MServiceRegistry::Tick()
{
    // MNetServerBase::Run 主循环驱动 EventLoop；这里不需要额外的 per-frame
    // 工作，但保留 Tick override 以便未来扩展。
}

void MServiceRegistry::TickBackends()
{
    TickHeartbeats();
}

void MServiceRegistry::ShutdownConnections()
{
    for (auto& [ConnId, Session] : Sessions_)
    {
        if (Session && Session->Conn && Session->Conn->IsConnected())
        {
            Session->Conn->Close();
        }
    }
    Sessions_.clear();
    Endpoints_.clear();
}

void MServiceRegistry::Dispose()
{
    if (IsDisposed()) return;
    MarkDisposed();
    ShutdownConnections();
}
