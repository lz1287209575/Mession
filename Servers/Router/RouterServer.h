#pragma once

#include "Core/NetCore.h"
#include "Core/Socket.h"
#include "Common/Logger.h"
#include "Common/ServerConnection.h"
#include "Common/ServerMessages.h"
#include <thread>
#include <chrono>

struct SRouterConfig
{
    uint16 ListenPort = 8005;
};

struct SRouterPeer
{
    TSharedPtr<MTcpConnection> Connection;
    bool bAuthenticated = false;
    bool bRegistered = false;
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    FString ServerName;
    FString Address = "127.0.0.1";
    uint16 Port = 0;
};

struct SPlayerRouteBinding
{
    uint64 PlayerId = 0;
    uint32 WorldServerId = 0;
};

class MRouterServer
{
private:
    int32 ListenSocket = -1;
    bool bRunning = false;
    SRouterConfig Config;
    TMap<uint64, SRouterPeer> Peers;
    TMap<uint64, SPlayerRouteBinding> PlayerRouteBindings;
    uint64 NextConnectionId = 1;

public:
    MRouterServer() = default;
    ~MRouterServer() { Shutdown(); }

    bool Init(int InPort);
    void Shutdown();
    void Tick();
    void Run();

private:
    void AcceptServers();
    void ProcessMessages();
    void HandlePacket(uint64 ConnectionId, const TArray& Data);
    bool SendServerMessage(uint64 ConnectionId, uint8 Type, const TArray& Payload);
    template<typename TMessage>
    bool SendServerMessage(uint64 ConnectionId, EServerMessageType Type, const TMessage& Message)
    {
        return SendServerMessage(ConnectionId, static_cast<uint8>(Type), BuildPayload(Message));
    }
    const SRouterPeer* SelectRouteTarget(EServerType RequestedType, uint64 PlayerId);
    const SRouterPeer* FindRegisteredServerById(uint32 ServerId) const;
    void RemovePeer(uint64 ConnectionId);
};
