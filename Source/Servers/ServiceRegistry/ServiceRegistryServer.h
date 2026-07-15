#pragma once

#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServiceDiscovery/Endpoint.h"
#include "Common/Net/ServiceDiscovery/RegistryProtocol.h"
#include "Common/Runtime/Object/IDisposable.h"
#include "Common/Runtime/Object/Object.h"
#include "Servers/ServiceRegistry/ServiceRegistryConfig.h"

class FRegistryClientSession : public TEnableSharedFromThis<FRegistryClientSession>
{
public:
    uint64 ConnId = 0;
    TSharedPtr<INetConnection> Conn;
    TByteArray RecvBuffer;
    uint32 ServerId = 0;          // 0 until Register succeeds
    bool bRegistered = false;
    uint64 LastSeenMs = 0;
};

MCLASS(Type=Service)
class MServiceRegistry : public MNetServerBase, public MObject, public IDisposable
{
public:
    MGENERATED_BODY(MServiceRegistry, MObject, 0)
public:
    using MObject::Tick;

    bool Init(int InPort = 0);
    void Tick();
    void TickBackends() override;
    void ShutdownConnections() override;
    void OnRunStarted() override;
    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;

    void Dispose() override;

private:
    // 1-byte type + N-byte payload 解析；调用方传入已剥掉外层 length-prefix 的包体
    void HandlePacket(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Packet);
    void HandleRegister(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload);
    void HandleDeregister(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload);
    void HandleHeartbeat(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload);
    void HandleUpdateActors(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload);
    void HandleListEndpoints(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload);

    void SendTo(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Packet);
    void SendAck(TSharedPtr<FRegistryClientSession> Session, EServiceRegistryResult Status, const MString& Message);
    void SendEndpointChange(EServerType ServerType);

    void TickHeartbeats();

    SServiceRegistryConfig Config;
    TMap<uint32, FServiceEndpoint> Endpoints_;          // by ServerId
    TMap<EServerType, uint64> MonotonicSeq_;
    TMap<uint64, TSharedPtr<FRegistryClientSession>> Sessions_;  // by ConnId
    uint64 NextSeq_ = 0;
};