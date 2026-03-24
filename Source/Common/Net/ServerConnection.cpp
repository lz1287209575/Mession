#include "Common/Net/ServerConnection.h"
#include "Common/Net/Rpc/RpcManifest.h"

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

bool MServerConnection::SendPacket(uint8 PacketType, const void* Data, uint32 Size)
{
    if (State != EConnectionState::Connected && State != EConnectionState::Authenticated)
    {
        return false;
    }
    
    TByteArray Payload;
    Payload.resize(1 + Size);
    Payload[0] = PacketType;
    if (Size > 0 && Data)
    {
        memcpy(Payload.data() + 1, Data, Size);
    }

    return SendPacketRaw(Payload);
}

bool MServerConnection::SendPacketRaw(const TByteArray& Data)
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
            const uint8 PacketType = Packet[0];
            TByteArray Payload(Packet.begin() + 1, Packet.end());
            HandlePacket(PacketType, Payload);
        }
    }

    if (Transport && !Transport->IsConnected())
    {
        LOG_INFO("%s Connection closed by remote", LogPrefix.c_str());
        Disconnect();
    }
}

void MServerConnection::HandlePacket(uint8 PacketType, const TByteArray& Data)
{
    switch (PacketType)
    {
        default:
        {
            // 转发给应用层
            if (OnMessageCallback)
            {
                OnMessageCallback(shared_from_this(), PacketType, Data);
            }
            break;
        }
    }
}

void MServerConnection::SendHandshake()
{
    const char* ClassName = GetServerEndpointClassName(Config.ServerType);
    if (!ClassName)
    {
        LOG_WARN("%s No RPC handshake endpoint for server type %d", LogPrefix.c_str(), static_cast<int>(Config.ServerType));
        return;
    }

    if (!MRpc::CallRemote(
            *this,
            ClassName,
            "Rpc_OnServerHandshake",
            LocalServerInfo.ServerId,
            static_cast<uint8>(LocalServerInfo.ServerType),
            LocalServerInfo.ServerName))
    {
        LOG_WARN("%s Handshake RPC send failed", LogPrefix.c_str());
        return;
    }

    State = EConnectionState::Authenticated;
    LOG_INFO("%s Authentication successful!", LogPrefix.c_str());
    if (OnServerAuthenticatedCallback)
    {
        OnServerAuthenticatedCallback(shared_from_this(), GetRemoteServerInfo());
    }
}

void MServerConnection::SendHeartbeat()
{
    const char* ClassName = GetServerEndpointClassName(Config.ServerType);
    if (!ClassName)
    {
        return;
    }

    MRpc::CallRemote(*this, ClassName, "Rpc_OnHeartbeat", ++HeartbeatSeq);
}
