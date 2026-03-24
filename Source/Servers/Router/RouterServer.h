#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Log/Logger.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Router/RouterServiceMessages.h"
#include "Servers/Router/Services/RouterRegistryServiceEndpoint.h"

struct SRouterConfig
{
    uint16 ListenPort = 8005;
};

MCLASS(Type=Server)
class MRouterServer : public MNetServerBase, public MObject
{
public:
    MGENERATED_BODY(MRouterServer, MObject, 0)
public:
    using MObject::Tick;

    bool LoadConfig(const MString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void ShutdownConnections() override;
    void OnRunStarted() override;

private:
    void HandlePeerPacket(uint64 ConnectionId, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data);
    SRouterConfig Config;
    TMap<uint64, SPlayerRouteRecord> Routes;
    TMap<uint64, TSharedPtr<INetConnection>> PeerConnections;
    MRouterRegistryServiceEndpoint* RegistryService = nullptr;
};
