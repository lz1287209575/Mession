#pragma once

#include "../../Core/NetCore.h"
#include "../../Core/Socket.h"
#include "../../Common/Logger.h"
#include <map>
#include <memory>
#include <vector>
#include <random>
#include <thread>
#include <chrono>

// 登录服务器配置
struct FLoginConfig
{
    uint16 ListenPort = 8002;  // 网关连接端口
    uint32 SessionKeyMin = 100000;
    uint32 SessionKeyMax = 999999;
};

// 在线会话
struct FSession
{
    uint64 PlayerId;
    uint32 SessionKey;
    uint64 ConnectionId;
    uint64 ExpireTime;  // 过期时间戳
};

// 登录服务器
class FLoginServer
{
private:
    int32 ListenSocket = -1;
    bool bRunning = false;
    
    // 配置
    FLoginConfig Config;
    
    // 网关连接
    std::map<uint64, std::shared_ptr<FTcpConnection>> GatewayConnections;
    uint64 NextConnectionId = 1;
    
    // 会话管理
    std::map<uint32, FSession> Sessions;  // SessionKey -> Session
    std::map<uint64, uint32> PlayerSessions;  // PlayerId -> SessionKey
    
    // 随机数生成器
    std::mt19937 Rng;
    
public:
    FLoginServer();
    ~FLoginServer() { Shutdown(); }
    
    bool Init(int InPort);
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
    uint32 GenerateSessionKey();
};
