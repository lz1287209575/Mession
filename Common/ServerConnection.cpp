#include "ServerConnection.h"
#include "Common/ServerMessages.h"
#include <poll.h>

// 静态成员定义
SServerInfo MServerConnection::LocalServerInfo;

void MServerConnection::UpdateLogPrefix()
{
    LogPrefix = "[Server:" + Config.ServerName + "]";
}

bool MServerConnection::Connect()
{
    if (State == EConnectionState::Authenticated || State == EConnectionState::Connected)
    {
        return true;
    }
    
    if (State == EConnectionState::Connecting)
    {
        return false;
    }
    
    return TryConnect();
}

bool MServerConnection::TryConnect()
{
    if (Config.Address.empty() || Config.Port == 0)
    {
        LOG_WARN("%s Invalid address or port", LogPrefix.c_str());
        return false;
    }
    
    State = EConnectionState::Connecting;
    
    // 创建socket
    SocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (SocketFd < 0)
    {
        LOG_ERROR("%s Failed to create socket", LogPrefix.c_str());
        State = EConnectionState::Disconnected;
        return false;
    }
    
    // 设置非阻塞
    MSocket::SetNonBlocking(SocketFd, true);
    MSocket::SetNoDelay(SocketFd, true);
    
    // 连接
    sockaddr_in Addr = {};
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons(Config.Port);
    
    if (inet_pton(AF_INET, Config.Address.c_str(), &Addr.sin_addr) <= 0)
    {
        LOG_ERROR("%s Invalid address: %s", LogPrefix.c_str(), Config.Address.c_str());
        close(SocketFd);
        SocketFd = -1;
        State = EConnectionState::Disconnected;
        return false;
    }
    
    int Result = connect(SocketFd, (sockaddr*)&Addr, sizeof(Addr));
    
    if (Result < 0 && errno != EINPROGRESS)
    {
        LOG_ERROR("%s Connect failed: %s", LogPrefix.c_str(), strerror(errno));
        close(SocketFd);
        SocketFd = -1;
        State = EConnectionState::Disconnected;
        return false;
    }
    
    LOG_INFO("%s Connecting to %s:%d...", LogPrefix.c_str(), Config.Address.c_str(), Config.Port);
    
    // 等待连接成功
    pollfd Pfd;
    Pfd.fd = SocketFd;
    Pfd.events = POLLOUT;
    Pfd.revents = 0;
    
    int Ret = poll(&Pfd, 1, (int)(Config.ConnectTimeout * 1000));
    
    if (Ret > 0 && (Pfd.revents & POLLOUT))
    {
        State = EConnectionState::Connected;
        LOG_INFO("%s Connected to %s:%d!", LogPrefix.c_str(), Config.Address.c_str(), Config.Port);
        
        // 发送握手
        SendHandshake();
        
        if (OnConnectCallback)
        {
            OnConnectCallback(shared_from_this());
        }
        
        return true;
    }
    
    LOG_WARN("%s Connection timeout to %s:%d", LogPrefix.c_str(), Config.Address.c_str(), Config.Port);
    close(SocketFd);
    SocketFd = -1;
    State = EConnectionState::Disconnected;
    return false;
}

void MServerConnection::Disconnect()
{
    if (SocketFd >= 0)
    {
        close(SocketFd);
        SocketFd = -1;
    }
    
    State = EConnectionState::Disconnected;
    RecvBuffer.clear();
    HeartbeatTimer = 0.0f;
    
    LOG_INFO("%s Disconnected", LogPrefix.c_str());
    
    if (OnDisconnectCallback)
    {
        OnDisconnectCallback(shared_from_this());
    }
}

bool MServerConnection::Send(uint8 Type, const void* Data, uint32 Size)
{
    if (State != EConnectionState::Connected && State != EConnectionState::Authenticated)
    {
        return false;
    }
    
    // 消息格式: [Length(4)][Type(1)][Data...]
    TArray Packet;
    uint32 TotalSize = 1 + Size;
    Packet.resize(4 + TotalSize);
    
    *(uint32*)Packet.data() = TotalSize;
    Packet[4] = Type;
    if (Size > 0 && Data)
    {
        memcpy(Packet.data() + 5, Data, Size);
    }
    
    return SendRaw(Packet);
}

bool MServerConnection::SendRaw(const TArray& Data)
{
    if (SocketFd < 0 || Data.empty())
    {
        return false;
    }
    
    ssize_t Sent = send(SocketFd, Data.data(), Data.size(), 0);
    
    if (Sent < 0)
    {
        if (errno != EWOULDBLOCK && errno != EAGAIN)
        {
            LOG_ERROR("%s Send failed: %s", LogPrefix.c_str(), strerror(errno));
            Disconnect();
            return false;
        }
    }
    
    return true;
}

void MServerConnection::Tick(float DeltaTime)
{
    if (State == EConnectionState::Disconnected)
    {
        if (Config.Address.empty() || Config.Port == 0)
        {
            return;
        }

        // 尝试重连
        ReconnectTimer += DeltaTime;
        if (ReconnectTimer >= ReconnectInterval)
        {
            ReconnectTimer = 0.0f;
            LOG_INFO("%s Attempting to reconnect...", LogPrefix.c_str());
            TryConnect();
        }
        return;
    }
    
    if (State != EConnectionState::Connected && State != EConnectionState::Authenticated)
    {
        return;
    }
    
    // 处理接收
    ProcessRecv();
    
    // 心跳
    HeartbeatTimer += DeltaTime;
    if (HeartbeatTimer >= HeartbeatInterval)
    {
        HeartbeatTimer = 0.0f;
        SendHeartbeat();
    }
}

void MServerConnection::ProcessRecv()
{
    if (SocketFd < 0)
    {
        return;
    }
    
    uint8 Buffer[8192];
    ssize_t BytesRead = recv(SocketFd, Buffer, sizeof(Buffer), 0);
    
    if (BytesRead > 0)
    {
        RecvBuffer.insert(RecvBuffer.end(), Buffer, Buffer + BytesRead);
        
        // 处理粘包
        while (RecvBuffer.size() >= 4)
        {
            uint32 PacketSize = *(uint32*)RecvBuffer.data();
            
            if (PacketSize > 65535 || PacketSize == 0)
            {
                LOG_ERROR("%s Invalid packet size: %u", LogPrefix.c_str(), PacketSize);
                Disconnect();
                return;
            }
            
            if (RecvBuffer.size() < 4 + PacketSize)
            {
                break;
            }
            
            // 提取完整包
            TArray Packet(RecvBuffer.begin() + 4, RecvBuffer.begin() + 4 + PacketSize);
            RecvBuffer.erase(RecvBuffer.begin(), RecvBuffer.begin() + 4 + PacketSize);
            
            // 处理消息
            if (!Packet.empty())
            {
                uint8 Type = Packet[0];
                TArray Payload(Packet.begin() + 1, Packet.end());
                HandleMessage(Type, Payload);
            }
        }
    }
    else if (BytesRead == 0)
    {
        LOG_INFO("%s Connection closed by remote", LogPrefix.c_str());
        Disconnect();
    }
    else if (errno != EWOULDBLOCK && errno != EAGAIN)
    {
        LOG_ERROR("%s Recv error: %s", LogPrefix.c_str(), strerror(errno));
        Disconnect();
    }
}

void MServerConnection::HandleMessage(uint8 Type, const TArray& Data)
{
    switch (Type)
    {
        case (uint8)EServerMessageType::MT_ServerHandshake:
        {
            LOG_INFO("%s Received handshake", LogPrefix.c_str());
            SendHandshakeAck();
            break;
        }
        
        case (uint8)EServerMessageType::MT_ServerHandshakeAck:
        {
            State = EConnectionState::Authenticated;
            LOG_INFO("%s Authentication successful!", LogPrefix.c_str());
            
            // 获取远程服务器信息
            SServerInfo RemoteInfo = GetRemoteServerInfo();
            if (OnServerAuthenticatedCallback)
            {
                OnServerAuthenticatedCallback(shared_from_this(), RemoteInfo);
            }
            break;
        }
        
        case (uint8)EServerMessageType::MT_Heartbeat:
        {
            SendHeartbeatAck();
            break;
        }
        
        case (uint8)EServerMessageType::MT_HeartbeatAck:
        {
            LastHeartbeatTime = 0;
            LOG_DEBUG("%s Heartbeat OK", LogPrefix.c_str());
            break;
        }
        
        default:
        {
            // 转发给应用层
            if (OnMessageCallback)
            {
                OnMessageCallback(shared_from_this(), Type, Data);
            }
            break;
        }
    }
}

void MServerConnection::SendHandshake()
{
    const SServerHandshakeMessage Message{
        LocalServerInfo.ServerId,
        LocalServerInfo.ServerType,
        LocalServerInfo.ServerName
    };
    TArray Data = BuildPayload(Message);
    Send((uint8)EServerMessageType::MT_ServerHandshake, Data.data(), Data.size());
    
    LOG_INFO("%s Sent handshake to %s:%d", LogPrefix.c_str(), Config.Address.c_str(), Config.Port);
}

void MServerConnection::SendHandshakeAck()
{
    Send((uint8)EServerMessageType::MT_ServerHandshakeAck, nullptr, 0);
    State = EConnectionState::Authenticated;
    LOG_INFO("%s Sent handshake ack", LogPrefix.c_str());
}

void MServerConnection::SendHeartbeat()
{
    const SHeartbeatMessage Message{++HeartbeatSeq};
    TArray Data = BuildPayload(Message);
    Send((uint8)EServerMessageType::MT_Heartbeat, Data.data(), Data.size());
}

void MServerConnection::SendHeartbeatAck()
{
    Send((uint8)EServerMessageType::MT_HeartbeatAck, nullptr, 0);
}

// 便捷发送方法
bool MServerConnection::SendPlayerLogin(uint64 PlayerId, uint32 SessionKey)
{
    const SPlayerLoginResponseMessage Message{0, PlayerId, SessionKey};
    TArray Data = BuildPayload(Message);
    return Send((uint8)EServerMessageType::MT_PlayerLogin, Data.data(), Data.size());
}

bool MServerConnection::SendPlayerLogout(uint64 PlayerId)
{
    const SPlayerLogoutMessage Message{PlayerId};
    TArray Data = BuildPayload(Message);
    return Send((uint8)EServerMessageType::MT_PlayerLogout, Data.data(), Data.size());
}

bool MServerConnection::SendChatMessage(uint64 FromPlayerId, const FString& Message)
{
    const SChatMessage ChatMessage{FromPlayerId, Message};
    TArray Data = BuildPayload(ChatMessage);
    return Send((uint8)EServerMessageType::MT_ChatMessage, Data.data(), Data.size());
}

bool MServerConnection::Broadcast(uint8 Type, const void* Data, uint32 Size)
{
    return Send(Type, Data, Size);
}
