#pragma once

#include "../Core/NetCore.h"
#include "../NetDriver/Replicate.h"

// 网络消息类型
enum class ENetMessageType : uint8
{
    MT_Handshake = 1,        // 握手/连接
    MT_HandshakeResponse = 2, // 握手响应
    MT_Login = 3,             // 登录
    MT_LoginResponse = 4,     // 登录响应
    MT_Logout = 5,            // 登出
    MT_ActorCreate = 6,       // 创建Actor
    MT_ActorDestroy = 7,      // 销毁Actor
    MT_ActorUpdate = 8,       // Actor属性更新
    MT_RPC = 9,               // 远程过程调用
    MT_Chat = 10,             // 聊天消息
    MT_Heartbeat = 11,        // 心跳
    MT_Error = 12,            // 错误消息
};

// 消息头
struct FNetMessageHeader
{
    ENetMessageType Type;
    uint32 Size;
    uint32 Sequence;
    
    FNetMessageHeader() : Type(ENetMessageType::MT_Handshake), Size(0), Sequence(0) {}
    FNetMessageHeader(ENetMessageType InType, uint32 InSize, uint32 InSeq) 
        : Type(InType), Size(InSize), Sequence(InSeq) {}
};

// 握手消息
struct FHandshakeMessage
{
    uint32 ProtocolVersion;
    FString ClientName;
    
    void Serialize(FArchive& Ar)
    {
        Ar << ProtocolVersion;
        Ar << ClientName;
    }
};

// 登录消息
struct FLoginMessage
{
    uint64 PlayerId;
    FString Token;
    FString PlayerName;
    
    void Serialize(FArchive& Ar)
    {
        Ar << PlayerId;
        Ar << Token;
        Ar << PlayerName;
    }
};

// 登录响应
struct FLoginResponseMessage
{
    uint8 Result; // 0=成功, 1=失败
    uint64 AssignedPlayerId;
    FString Message;
    
    void Serialize(FArchive& Ar)
    {
        Ar << Result;
        Ar << AssignedPlayerId;
        Ar << Message;
    }
};

// 错误消息
struct FErrorMessage
{
    uint16 ErrorCode;
    FString ErrorMessage;
    
    void Serialize(FArchive& Ar)
    {
        Ar << ErrorCode;
        Ar << ErrorMessage;
    }
};

// 消息处理基类
class IMessageHandler
{
public:
    virtual ~IMessageHandler() = default;
    
    virtual void OnHandshake(uint64 ConnectionId, const FHandshakeMessage& Msg) = 0;
    virtual void OnLogin(uint64 ConnectionId, const FLoginMessage& Msg) = 0;
    virtual void OnLogout(uint64 ConnectionId) = 0;
    virtual void OnActorUpdate(uint64 ConnectionId, uint64 ActorId, const TArray& Data) = 0;
    virtual void OnHeartbeat(uint64 ConnectionId) = 0;
    virtual void OnError(uint64 ConnectionId, uint16 ErrorCode, const FString& ErrorMsg) = 0;
};

// 消息分发器
class FMessageDispatcher
{
private:
    IMessageHandler* Handler;
    std::map<ENetMessageType, std::function<void(uint64, FArchive&)>> Handlers;
    
public:
    FMessageDispatcher(IMessageHandler* InHandler) : Handler(InHandler)
    {
        RegisterHandlers();
    }
    
    void RegisterHandlers()
    {
        Handlers[ENetMessageType::MT_Handshake] = [this](uint64 ConnId, FArchive& Ar)
        {
            FHandshakeMessage Msg;
            Msg.Serialize(Ar);
            Handler->OnHandshake(ConnId, Msg);
        };
        
        Handlers[ENetMessageType::MT_Login] = [this](uint64 ConnId, FArchive& Ar)
        {
            FLoginMessage Msg;
            Msg.Serialize(Ar);
            Handler->OnLogin(ConnId, Msg);
        };
        
        Handlers[ENetMessageType::MT_Logout] = [this](uint64 ConnId, FArchive& Ar)
        {
            Handler->OnLogout(ConnId);
        };
        
        Handlers[ENetMessageType::MT_ActorUpdate] = [this](uint64 ConnId, FArchive& Ar)
        {
            uint64 ActorId;
            uint32 DataSize;
            Ar << ActorId;
            Ar << DataSize;
            
            TArray Data;
            if (DataSize > 0 && Ar.IsLoading())
            {
                Data.resize(DataSize);
                // 读取剩余数据
            }
            
            Handler->OnActorUpdate(ConnId, ActorId, Data);
        };
        
        Handlers[ENetMessageType::MT_Heartbeat] = [this](uint64 ConnId, FArchive& Ar)
        {
            Handler->OnHeartbeat(ConnId);
        };
    }
    
    void Dispatch(uint64 ConnectionId, const TArray& Data)
    {
        if (Data.empty() || !Handler)
            return;
        
        FMemoryArchive Ar(Data);
        
        // 读取消息类型
        uint8 TypeByte;
        Ar << TypeByte;
        ENetMessageType Type = (ENetMessageType)TypeByte;
        
        auto It = Handlers.find(Type);
        if (It != Handlers.end())
        {
            try
            {
                It->second(ConnectionId, Ar);
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Message dispatch error: %s", e.what());
            }
        }
        else
        {
            LOG_WARN("Unknown message type: %d", (int)Type);
        }
    }
};
