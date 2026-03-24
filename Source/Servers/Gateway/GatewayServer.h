#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Net/Rpc/RpcDispatch.h"
#include "Common/Runtime/Log/Logger.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Servers/Gateway/Rpc/GatewayBackendRpc.h"
#include "Servers/Gateway/Services/GatewayClientServiceEndpoint.h"

struct SGatewayConfig
{
    uint16 ListenPort = 8001;
    MString LoginServerAddr = "127.0.0.1";
    uint16 LoginServerPort = 8002;
    MString WorldServerAddr = "127.0.0.1";
    uint16 WorldServerPort = 8003;
};

MCLASS(Type=Server)
class MGatewayServer : public MNetServerBase, public MObject
{
public:
    MGENERATED_BODY(MGatewayServer, MObject, 0)
public:
    using MObject::Tick;

    bool LoadConfig(const MString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void TickBackends() override;
    void ShutdownConnections() override;
    void OnRunStarted() override;

private:
    void HandleClientPacket(uint64 ConnectionId, const TByteArray& Data);
    void HandleBackendPacket(uint8 PacketType, const TByteArray& Data, const char* PeerName);

    SGatewayConfig Config;
    TMap<uint64, TSharedPtr<INetConnection>> ClientConnections;
    MServerConnectionManager BackendConnectionManager;
    TSharedPtr<MServerConnection> LoginServerConn;
    TSharedPtr<MServerConnection> WorldServerConn;
    MGatewayLoginRpc* LoginRpc = nullptr;
    MGatewayWorldRpc* WorldRpc = nullptr;
    MGatewayClientServiceEndpoint* ClientService = nullptr;
};
