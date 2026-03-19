#include "ServerConnection.h"
#include "Common/ServerMessages.h"

// MTcpMessageChannel implementation
bool MTcpMessageChannel::Send(const void* Data, uint32 Size)
{
    return Connection && Connection->Send(Data, Size);
}

bool MTcpMessageChannel::ReceivePacket(TByteArray& OutPacket)
{
    return Connection && Connection->ReceivePacket(OutPacket);
}

bool MTcpMessageChannel::IsConnected() const
{
    return Connection && Connection->IsConnected();
}

void MTcpMessageChannel::Close()
{
    if (Connection)
    {
        Connection->Close();
    }
}

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

    if (!MSocket::EnsureInit())
    {
        LOG_ERROR("%s Platform init failed", LogPrefix.c_str());
        State = EConnectionState::Disconnected;
        return false;
    }

    TSharedPtr<MTcpConnection> TcpConn = MTcpConnection::ConnectTo(SSocketAddress(Config.Address, Config.Port), Config.ConnectTimeout);
    if (!TcpConn || !TcpConn->IsConnected())
    {
        LOG_ERROR("%s Connect failed", LogPrefix.c_str());
        TcpConn.reset();
        Transport.reset();
        State = EConnectionState::Disconnected;
        return false;
    }

    // 将底层 TCP 连接包装为消息通道
    Transport = MakeShared<MTcpMessageChannel>(TcpConn);

    LOG_INFO("%s Connecting to %s:%d...", LogPrefix.c_str(), Config.Address.c_str(), Config.Port);
    State = EConnectionState::Connected;
    LOG_INFO("%s Connected to %s:%d!", LogPrefix.c_str(), Config.Address.c_str(), Config.Port);

    SendHandshake();

    if (OnConnectCallback)
    {
        OnConnectCallback(shared_from_this());
    }

    return true;
}

void MServerConnection::Disconnect()
{
    if (Transport)
    {
        Transport->Close();
        Transport.reset();
    }
    
    State = EConnectionState::Disconnected;
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
    
    TByteArray Payload;
    Payload.resize(1 + Size);
    Payload[0] = Type;
    if (Size > 0 && Data)
    {
        memcpy(Payload.data() + 1, Data, Size);
    }

    return SendRaw(Payload);
}

bool MServerConnection::SendRaw(const TByteArray& Data)
{
    if (!Transport || !Transport->IsConnected() || Data.empty())
    {
        return false;
    }

    const uint32 Size = static_cast<uint32>(Data.size());
    if (Transport->Send(Data.data(), Size))
    {
        BytesSent += Size;
        return true;
    }
    return false;
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
            ++ReconnectAttempts;
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
    if (!Transport)
    {
        return;
    }

    TByteArray Packet;
    while (Transport->ReceivePacket(Packet))
    {
        if (!Packet.empty())
        {
            BytesReceived += static_cast<uint64>(Packet.size());
            const uint8 Type = Packet[0];
            TByteArray Payload(Packet.begin() + 1, Packet.end());
            HandleMessage(Type, Payload);
        }
    }

    if (Transport && !Transport->IsConnected())
    {
        LOG_INFO("%s Connection closed by remote", LogPrefix.c_str());
        Disconnect();
    }
}

void MServerConnection::HandleMessage(uint8 Type, const TByteArray& Data)
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
    TByteArray Data = BuildPayload(Message);
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
    TByteArray Data = BuildPayload(Message);
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
    TByteArray Data = BuildPayload(Message);
    return Send((uint8)EServerMessageType::MT_PlayerLogin, Data.data(), Data.size());
}

bool MServerConnection::SendPlayerLogout(uint64 PlayerId)
{
    const SPlayerLogoutMessage Message{PlayerId};
    TByteArray Data = BuildPayload(Message);
    return Send((uint8)EServerMessageType::MT_PlayerLogout, Data.data(), Data.size());
}

bool MServerConnection::SendChatMessage(uint64 FromPlayerId, const MString& Message)
{
    const SChatMessage ChatMessage{FromPlayerId, Message};
    TByteArray Data = BuildPayload(ChatMessage);
    return Send((uint8)EServerMessageType::MT_ChatMessage, Data.data(), Data.size());
}

bool MServerConnection::Broadcast(uint8 Type, const void* Data, uint32 Size)
{
    return Send(Type, Data, Size);
}
