#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Net/ServerRpcRuntime.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/AppMessages.h"
#include "Protocol/Messages/AuthSessionMessages.h"
#include "Protocol/Messages/WorldPlayerMessages.h"
#include "Protocol/Messages/ClientCallMessages.h"

struct SGatewayConfig
{
    uint16 ListenPort = 8001;
    MString LoginServerAddr = "127.0.0.1";
    uint16 LoginServerPort = 8002;
    MString WorldServerAddr = "127.0.0.1";
    uint16 WorldServerPort = 8003;
};

MCLASS()
class MGatewayServer : public MNetServerBase, public MObject, public IGeneratedClientResponseTarget
{
public:
    MGENERATED_BODY(MGatewayServer, MObject, 0)
public:
    using MObject::Tick;
    IGeneratedClientResponseTarget* GetGeneratedClientResponseTarget() override { return this; }

    bool LoadConfig(const MString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void TickBackends() override;
    void ShutdownConnections() override;
    void OnRunStarted() override;
    bool SendGeneratedClientResponse(uint64 ConnectionId, uint16 FunctionId, uint64 CallId, const TByteArray& Payload) override;

    MFUNCTION(ClientCall)
    void Client_Echo(FClientEchoRequest& Request, FClientEchoResponse& Response);

    MFUNCTION(ClientCall)
    void Client_Login(FClientLoginRequest& Request, FClientLoginResponse& Response);

    MFUNCTION(ClientCall)
    void Client_FindPlayer(FClientFindPlayerRequest& Request, FClientFindPlayerResponse& Response);

    MFUNCTION(ClientCall)
    void Client_Logout(FClientLogoutRequest& Request, FClientLogoutResponse& Response);

    MFUNCTION(ClientCall)
    void Client_SwitchScene(FClientSwitchSceneRequest& Request, FClientSwitchSceneResponse& Response);

private:
    const MClass* GetLoginServerClass() const;
    const MClass* GetWorldServerClass() const;
    void HandleClientPacket(uint64 ConnectionId, const TByteArray& Data);
    void HandleBackendPacket(uint8 PacketType, const TByteArray& Data, const char* PeerName);

    SGatewayConfig Config;
    TMap<uint64, TSharedPtr<INetConnection>> ClientConnections;
    MServerConnectionManager BackendConnectionManager;
    TSharedPtr<MServerConnection> LoginServerConn;
    TSharedPtr<MServerConnection> WorldServerConn;
};
