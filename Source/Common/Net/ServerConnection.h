#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Runtime/Log/Logger.h"
#include <thread>
#include <chrono>

// 服务器类型
enum class EServerType : uint8
{
    Unknown = 0,
    Gateway = 1,
    Login = 2,
    World = 3,
    Scene = 4,
    Router = 5,
    Mgo = 6
};

// 服务器间消息类型
enum class EServerMessageType : uint8
{
    MT_RPC = 27,                  // 服务器间 RPC 调用
    MT_FunctionCall = 28,         // 服务器间反射函数调用请求
    MT_FunctionResponse = 29,     // 服务器间反射函数调用响应
};

// 服务器信息
struct SServerInfo
{
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    MString ServerName;
    MString Address;
    uint16 Port = 0;
    uint16 ZoneId = 0;

    SServerInfo() = default;
    SServerInfo(uint32 Id, EServerType Type, const MString& Name, const MString& Addr, uint16 P, uint16 Z = 0)
        : ServerId(Id), ServerType(Type), ServerName(Name), Address(Addr), Port(P), ZoneId(Z) {}
};

// 用于 ApplyRoute 等需要完整 ServerInfo 的场景
inline SServerInfo MakeServerInfo(uint32 Id, EServerType Type, const MString& Name, const MString& Addr, uint16 Port, uint16 ZoneId = 0)
{
    SServerInfo Info;
    Info.ServerId = Id;
    Info.ServerType = Type;
    Info.ServerName = Name;
    Info.Address = Addr;
    Info.Port = Port;
    Info.ZoneId = ZoneId;
    return Info;
}

// 服务器连接状态
enum class EConnectionState
{
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    Authenticated = 3
};

// 服务器连接配置
struct SServerConnectionConfig
{
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    MString ServerName;
    MString Address;
    uint16 Port = 0;
    
    // 心跳配置
    float HeartbeatInterval = 5.0f;  // 心跳间隔（秒）
    float ConnectTimeout = 3.0f;     // 连接超时
    float ReconnectInterval = 5.0f;  // 重连间隔
    
    SServerConnectionConfig() = default;
    
    SServerConnectionConfig(uint32 Id, EServerType Type, const MString& Name, 
                           const MString& Addr, uint16 P)
        : ServerId(Id), ServerType(Type), ServerName(Name), Address(Addr), Port(P) {}
};

// 消息通道抽象：屏蔽具体传输实现（TCP/UDP/WebSocket等）
class MMessageChannel
{
public:
    virtual ~MMessageChannel() = default;
    virtual bool Send(const void* Data, uint32 Size) = 0;
    virtual bool ReceivePacket(TByteArray& OutPacket) = 0;
    virtual bool IsConnected() const = 0;
    virtual void Close() = 0;
};

// TCP 消息通道实现（基于 MTcpConnection）
class MTcpMessageChannel : public MMessageChannel
{
public:
    explicit MTcpMessageChannel(TSharedPtr<MTcpConnection> InConnection)
        : Connection(std::move(InConnection))
    {
    }

    bool Send(const void* Data, uint32 Size) override;
    bool ReceivePacket(TByteArray& OutPacket) override;
    bool IsConnected() const override;
    void Close() override;

private:
    TSharedPtr<MTcpConnection> Connection;
};

// 单个服务器连接
class MServerConnection : public TEnableSharedFromThis<MServerConnection>
{
private:
    TSharedPtr<MMessageChannel> Transport;
    EConnectionState State = EConnectionState::Disconnected;
    SServerConnectionConfig Config;
    
    // 本地服务器信息（静态）
    static SServerInfo LocalServerInfo;
    
    // 心跳
    float HeartbeatTimer = 0.0f;
    float HeartbeatInterval = 5.0f;
    uint32 HeartbeatSeq = 0;
    
    // 重连
    float ReconnectTimer = 0.0f;
    float ReconnectInterval = 5.0f;
    uint32 ReconnectAttempts = 0;

    // 统计
    uint64 BytesSent = 0;
    uint64 BytesReceived = 0;
    
    // 回调
    TFunction<void(TSharedPtr<MServerConnection>)> OnConnectCallback;
    TFunction<void(TSharedPtr<MServerConnection>)> OnDisconnectCallback;
    TFunction<void(TSharedPtr<MServerConnection>, uint8, const TByteArray&)> OnMessageCallback;
    TFunction<void(TSharedPtr<MServerConnection>, const SServerInfo&)> OnServerAuthenticatedCallback;
    
    // 日志前缀
    MString LogPrefix;
    
public:
    MServerConnection() {}
    explicit MServerConnection(const SServerConnectionConfig& InConfig) : Config(InConfig) 
    {
        UpdateLogPrefix();
    }
    ~MServerConnection() { Disconnect(); }
    
    // 配置
    void SetConfig(const SServerConnectionConfig& InConfig) 
    { 
        Config = InConfig; 
        HeartbeatInterval = InConfig.HeartbeatInterval;
        ReconnectInterval = InConfig.ReconnectInterval;
        UpdateLogPrefix();
    }
    const SServerConnectionConfig& GetConfig() const { return Config; }
    
    // 静态方法：设置本服务器信息
    static void SetLocalInfo(uint32 Id, EServerType Type, const MString& Name)
    {
        LocalServerInfo.ServerId = Id;
        LocalServerInfo.ServerType = Type;
        LocalServerInfo.ServerName = Name;
    }
    
    // 回调设置
    void SetOnConnect(TFunction<void(TSharedPtr<MServerConnection>)> CB) { OnConnectCallback = CB; }
    void SetOnDisconnect(TFunction<void(TSharedPtr<MServerConnection>)> CB) { OnDisconnectCallback = CB; }
    void SetOnMessage(TFunction<void(TSharedPtr<MServerConnection>, uint8, const TByteArray&)> CB) { OnMessageCallback = CB; }
    void SetOnAuthenticated(TFunction<void(TSharedPtr<MServerConnection>, const SServerInfo&)> CB) { OnServerAuthenticatedCallback = CB; }
    
    // 连接/断开
    bool Connect();
    void Disconnect();
    bool IsConnected() const { return State == EConnectionState::Authenticated; }
    bool IsConnecting() const { return State == EConnectionState::Connecting || State == EConnectionState::Connected; }
    EConnectionState GetState() const { return State; }

    // 统计查询
    uint64 GetBytesSent() const { return BytesSent; }
    uint64 GetBytesReceived() const { return BytesReceived; }
    uint32 GetReconnectAttempts() const { return ReconnectAttempts; }
    
    // 发送底层包
    bool SendPacket(uint8 PacketType, const void* Data, uint32 Size);
    bool SendPacketRaw(const TByteArray& Data);
    
    // 更新（主循环调用）
    void Tick(float DeltaTime);
    
    // 获取服务器信息
    const SServerInfo& GetRemoteServerInfo() const 
    { 
        static SServerInfo Info;
        Info.ServerId = Config.ServerId;
        Info.ServerType = Config.ServerType;
        Info.ServerName = Config.ServerName;
        Info.Address = Config.Address;
        Info.Port = Config.Port;
        return Info;
    }
    
private:
    void UpdateLogPrefix();
    bool TryConnect();
    void ProcessRecv();
    void HandlePacket(uint8 PacketType, const TByteArray& Data);
    void SendHandshake();
    void SendHeartbeat();
};

// 服务器连接管理统计
struct SConnectionManagerStats
{
    size_t Total = 0;
    size_t Active = 0;
    uint64 BytesSent = 0;
    uint64 BytesReceived = 0;
    uint32 ReconnectAttempts = 0;
};

// 服务器连接管理器
// 说明：当前 Gateway/World/Scene 已部分迁移到显式后端连接持有模式，
// 若后续需要统一重连、心跳、连接池，可结合 Docs/Roadmap.md 继续收敛。
class MServerConnectionManager
{
private:
    // 所有服务器连接
    TMap<uint32, TSharedPtr<MServerConnection>> Connections;
    
    // 轮询间隔
    float PollInterval = 0.1f;
    float PollTimer = 0.0f;
    
public:
    MServerConnectionManager() {}
    
    // 添加远程服务器
    TSharedPtr<MServerConnection> AddServer(const SServerConnectionConfig& Config)
    {
        auto Conn = MakeShared<MServerConnection>(Config);
        Connections[Config.ServerId] = Conn;
        
        LOG_INFO("[ServerMgr] Added server: %s (%s:%d)", 
                 Config.ServerName.c_str(), Config.Address.c_str(), Config.Port);
        
        return Conn;
    }
    
    // 添加远程服务器（便捷方法）
    TSharedPtr<MServerConnection> AddServer(uint32 ServerId, EServerType Type, 
                                                   const MString& Name, 
                                                   const MString& Addr, uint16 Port)
    {
        SServerConnectionConfig Config(ServerId, Type, Name, Addr, Port);
        return AddServer(Config);
    }
    
    // 移除服务器
    void RemoveServer(uint32 ServerId)
    {
        auto It = Connections.find(ServerId);
        if (It != Connections.end())
        {
            It->second->Disconnect();
            Connections.erase(It);
            LOG_INFO("[ServerMgr] Removed server: %d", ServerId);
        }
    }
    
    // 获取连接
    TSharedPtr<MServerConnection> GetConnection(uint32 ServerId)
    {
        auto It = Connections.find(ServerId);
        return (It != Connections.end()) ? It->second : nullptr;
    }
    
    // 向指定服务器发送消息
    bool SendPacketToServer(uint32 ServerId, uint8 PacketType, const void* Data, uint32 Size)
    {
        auto Conn = GetConnection(ServerId);
        if (Conn && Conn->IsConnected())
        {
            return Conn->SendPacket(PacketType, Data, Size);
        }
        return false;
    }
    
    // 向所有服务器广播
    void BroadcastPacket(uint8 PacketType, const void* Data, uint32 Size)
    {
        for (auto& [Id, Conn] : Connections)
        {
            if (Conn->IsConnected())
            {
                Conn->SendPacket(PacketType, Data, Size);
            }
        }
    }
    
    // 轮询更新
    void Tick(float DeltaTime)
    {
        PollTimer += DeltaTime;
        if (PollTimer >= PollInterval)
        {
            PollTimer = 0.0f;
            
            for (auto& [Id, Conn] : Connections)
            {
                Conn->Tick(PollInterval);
            }
        }
    }
    
    // 连接所有服务器
    void ConnectAll()
    {
        for (auto& [Id, Conn] : Connections)
        {
            if (!Conn->IsConnected())
            {
                Conn->Connect();
            }
        }
    }
    
    // 断开所有服务器
    void DisconnectAll()
    {
        for (auto& [Id, Conn] : Connections)
        {
            Conn->Disconnect();
        }
    }
    
    // 获取统计
    size_t GetTotalCount() const { return Connections.size(); }
    size_t GetActiveCount() const
    {
        size_t Count = 0;
        for (auto& [Id, Conn] : Connections)
        {
            if (Conn->IsConnected())
            {
                Count++;
            }
        }
        return Count;
    }

    SConnectionManagerStats GetStats() const
    {
        SConnectionManagerStats Stats;
        Stats.Total = Connections.size();
        for (const auto& [Id, Conn] : Connections)
        {
            (void)Id;
            if (!Conn)
            {
                continue;
            }
            if (Conn->IsConnected())
            {
                ++Stats.Active;
            }
            Stats.BytesSent += Conn->GetBytesSent();
            Stats.BytesReceived += Conn->GetBytesReceived();
            Stats.ReconnectAttempts += Conn->GetReconnectAttempts();
        }
        return Stats;
    }
};
