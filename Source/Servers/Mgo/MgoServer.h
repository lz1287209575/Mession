#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Net/ServerRpcRuntime.h"
#include "Common/Runtime/Log/Logger.h"
#include "Protocol/Messages/AppMessages.h"
#include "Protocol/Messages/BackendServiceMessages.h"
#include "Servers/App/MgoPlayerStateService.h"

struct SMgoConfig
{
    uint16 ListenPort = 8006;
};

MCLASS()
class MMgoServer : public MNetServerBase, public MObject
{
public:
    MGENERATED_BODY(MMgoServer, MObject, 0)
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

    MFUNCTION(ServerCall, Target=Mgo)
    MFuture<TResult<FMgoLoadPlayerResponse, FAppError>> LoadPlayer(const FMgoLoadPlayerRequest& Request);

    MFUNCTION(ServerCall, Target=Mgo)
    MFuture<TResult<FMgoSavePlayerResponse, FAppError>> SavePlayer(const FMgoSavePlayerRequest& Request);

private:
    void HandlePeerPacket(uint64 ConnectionId, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data);
    SMgoConfig Config;
    TMap<uint64, MString> PlayerPayloads;
    TMap<uint64, TSharedPtr<INetConnection>> PeerConnections;
    MMgoPlayerStateService PlayerStateService;
};
