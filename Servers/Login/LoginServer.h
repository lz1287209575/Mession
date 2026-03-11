#pragma once

#include "Core/NetCore.h"
#include "Core/Socket.h"
#include "Common/Logger.h"
#include "Common/ServerConnection.h"
#include "Common/ServerMessages.h"
#include <random>
#include <thread>
#include <chrono>

// 登录服务器配置
struct SLoginConfig
{
    uint16 ListenPort = 8002;  // 网关连接端口
    FString RouterServerAddr = "127.0.0.1";
    uint16 RouterServerPort = 8005;
    uint32 SessionKeyMin = 100000;
    uint32 SessionKeyMax = 999999;
};

// 在线会话
struct SSession
{
    uint64 PlayerId;
    uint32 SessionKey;
    uint64 ConnectionId;
    uint64 ExpireTime;  // 过期时间戳
};

struct SGatewayPeer
{
    TSharedPtr<INetConnection> Connection;
    bool bAuthenticated = false;
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    FString ServerName;
};

// 登录服务器
class MLoginServer
{
private:
    TSocketFd ListenSocket = INVALID_SOCKET_FD;
    bool bRunning = false;
    bool bShutdownDone = false;
    
    // 配置
    SLoginConfig Config;
    
    // 网关连接
    TMap<uint64, SGatewayPeer> GatewayConnections;
    uint64 NextConnectionId = 1;

    TSharedPtr<MServerConnection> RouterServerConn;
    
    // 会话管理
    TMap<uint32, SSession> Sessions;  // SessionKey -> Session
    TMap<uint64, uint32> PlayerSessions;  // PlayerId -> SessionKey
    
    // 随机数生成器
    std::mt19937 Rng;
    
public:
    MLoginServer();
    ~MLoginServer() { Shutdown(); }
    
    bool LoadConfig(const FString& ConfigPath);
    bool Init(int InPort = 0);
    void RequestShutdown();
    void Shutdown();
    void Tick();
    void Run();
    
    // 会话管理
    uint32 CreateSession(uint64 PlayerId, uint64 ConnectionId);
    bool ValidateSession(uint32 SessionKey, uint64& OutPlayerId);
    void RemoveSession(uint32 SessionKey);
    
private:
    void AcceptGateways();
    void ProcessGatewayMessages();
    void HandleGatewayPacket(uint64 ConnectionId, const TArray& Data);
    bool SendServerMessage(uint64 ConnectionId, uint8 Type, const TArray& Payload);
    template<typename TMessage>
    bool SendServerMessage(uint64 ConnectionId, EServerMessageType Type, const TMessage& Message)
    {
        return SendServerMessage(ConnectionId, static_cast<uint8>(Type), BuildPayload(Message));
    }
    void HandleRouterServerMessage(uint8 Type, const TArray& Data);
    void SendRouterRegister();
    uint32 GenerateSessionKey();
};
