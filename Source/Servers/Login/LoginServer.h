#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/Rpc/RpcRuntimeContext.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Auth/AuthSessionMessages.h"
#include "Servers/Login/LoginSession.h"

struct SLoginConfig
{
    uint16 ListenPort = 8002;
};

MCLASS(Type=Server)
class MLoginServer : public MNetServerBase, public MObject, public MServerRuntimeContext
{
public:
    MGENERATED_BODY(MLoginServer, MObject, 0)
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

    MFUNCTION(ServerCall)
    MFuture<TResult<FLoginIssueSessionResponse, FAppError>> IssueSession(const FLoginIssueSessionRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FLoginValidateSessionResponse, FAppError>> ValidateSession(const FLoginValidateSessionRequest& Request);

private:
    void HandlePeerPacket(uint64 ConnectionId, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data);
    uint32 AllocateSessionKey();

private:
    SLoginConfig Config;
    uint32 NextSessionKey = 1000;
    TMap<uint64, uint32> Sessions;
    TMap<uint64, TSharedPtr<INetConnection>> PeerConnections;
    MLoginSession* Session = nullptr;
};

