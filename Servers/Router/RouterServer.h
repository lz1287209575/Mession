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
    uint32 RouteLeaseSeconds = 300;
};

struct SRouterPeer
{
    TSharedPtr<INetConnection> Connection;
    bool bAuthenticated = false;
    bool bRegistered = false;
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    FString ServerName;
    FString Address = "127.0.0.1";
    uint16 Port = 0;
    uint16 ZoneId = 0;
    uint32 CurrentLoad = 0;
    uint32 Capacity = 0;
};

struct SPlayerRouteBinding
{
    uint64 PlayerId = 0;
    uint32 WorldServerId = 0;
    uint64 LeaseExpireTick = 0;
};

class MRouterServer
{
private:
    TSocketFd ListenSocket = INVALID_SOCKET_FD;
    bool bRunning = false;
    bool bShutdownDone = false;
    SRouterConfig Config;
    TMap<uint64, SRouterPeer> Peers;
    TMap<uint64, SPlayerRouteBinding> PlayerRouteBindings;
    uint64 NextConnectionId = 1;
    uint64 TickCounter = 0;

public:
    MRouterServer() = default;
    ~MRouterServer() { Shutdown(); }

    bool LoadConfig(const FString& ConfigPath);
    bool Init(int InPort = 0);
    void RequestShutdown();
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
    const SRouterPeer* SelectRouteTarget(EServerType RequestedType, uint64 PlayerId, uint16 ZoneId = 0);
    const SRouterPeer* FindRegisteredServerById(uint32 ServerId) const;
    void RemovePeer(uint64 ConnectionId);
};
