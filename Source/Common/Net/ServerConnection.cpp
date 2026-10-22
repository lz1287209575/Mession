/**
 * @file ServerConnection.cpp
 * @brief 实现 MServerConnection 的建连/握手/心跳/重连与收发循环,以及 MTcpMessageChannel 转发。
 */

#include "Common/Net/ServerConnection.h"
#include "Common/Net/Rpc/RpcManifest.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Protocol/Messages/Common/ControlPlaneMessages.h"

// MTcpMessageChannel:对 MTcpConnection 的纯转发,内部 Connection 允许为空。
bool MTcpMessageChannel::Send(const void* Data, uint32 Size) {
    return Connection && Connection->Send(Data, Size);
}

bool MTcpMessageChannel::ReceivePacket(TByteArray& OutPacket) {
    return Connection && Connection->ReceivePacket(OutPacket);
}

bool MTcpMessageChannel::IsConnected() const {
    return Connection && Connection->IsConnected();
}

void MTcpMessageChannel::Close() {
    if (Connection) {
        Connection->Close();
    }
}

// 进程级静态成员:本机服务器信息,由 SetLocalInfo 在启动阶段写入。
SServerInfo MServerConnection::LocalServerInfo;

void MServerConnection::UpdateLogPrefix() {
    LogPrefix = "[Server:" + Config.ServerName + "]";
}

// 幂等入口:已在 Connected/Authenticated 直接成功;正在 Connecting 时返回 false
// (避免并发发起两次建连);其余情况交给 TryConnect。
bool MServerConnection::Connect() {
    if (State == EConnectionState::Authenticated || State == EConnectionState::Connected) {
        return true;
    }

    if (State == EConnectionState::Connecting) {
        return false;
    }

    return TryConnect();
}

bool MServerConnection::TryConnect() {
    if (Config.Address.empty() || Config.Port == 0) {
        LOG_WARN("%s Invalid address or port", LogPrefix.c_str());
        return false;
    }

    // 先置 Connecting 再尝试,确保中途失败/重入时状态不会停留在"看起来没在连"。
    State = EConnectionState::Connecting;

    if (!MSocket::EnsureInit()) {
        LOG_ERROR("%s Platform init failed", LogPrefix.c_str());
        State = EConnectionState::Disconnected;
        return false;
    }

    TSharedPtr<MTcpConnection> TcpConn = MTcpConnection::ConnectTo(SSocketAddress(Config.Address, Config.Port), Config.ConnectTimeout);
    if (!TcpConn || !TcpConn->IsConnected()) {
        LOG_ERROR("%s Connect failed", LogPrefix.c_str());
        TcpConn.reset();
        Transport.reset();
        State = EConnectionState::Disconnected;
        return false;
    }

    // 把底层 TCP 连接包装成消息通道
    Transport = MakeShared<MTcpMessageChannel>(TcpConn);

    LOG_INFO("%s Connecting to %s:%d...", LogPrefix.c_str(), Config.Address.c_str(), Config.Port);
    State = EConnectionState::Connected;
    LOG_INFO("%s Connected to %s:%d!", LogPrefix.c_str(), Config.Address.c_str(), Config.Port);

    // 握手完成前保持 Connected；仅在收到 ServerCall 成功响应后才对外可用。
    SendHandshake();

    if (OnConnectCallback) {
        OnConnectCallback(shared_from_this());
    }

    return true;
}

// 无幂等保护:每次调用都会复位状态/计时器并触发一次 OnDisconnect 回调,
// 析构函数也会走到这里(详见头文件里的 NOTE)。
void MServerConnection::Disconnect() {
    if (Transport) {
        Transport->Close();
        Transport.reset();
    }

    State          = EConnectionState::Disconnected;
    HeartbeatTimer = 0.0f;

    LOG_INFO("%s Disconnected", LogPrefix.c_str());

    if (OnDisconnectCallback) {
        OnDisconnectCallback(shared_from_this());
    }
}

bool MServerConnection::SendPacket(uint8 PacketType, const void* Data, uint32 Size) {
    if (State != EConnectionState::Connected && State != EConnectionState::Authenticated) {
        return false;
    }

    // 统一在这里拼"1 字节类型 + 负载",下游 SendPacketRaw 无需关心包类型。
    TByteArray Payload;
    Payload.resize(1 + Size);
    Payload[0] = PacketType;
    if (Size > 0 && Data) {
        memcpy(Payload.data() + 1, Data, Size);
    }

    return SendPacketRaw(Payload);
}

// 空包直接拒绝:调用方若要发"空消息",应通过 SendPacket 带上类型字节。
bool MServerConnection::SendPacketRaw(const TByteArray& Data) {
    if (!Transport || !Transport->IsConnected() || Data.empty()) {
        return false;
    }

    const uint32 Size = static_cast<uint32>(Data.size());
    if (Transport->Send(Data.data(), Size)) {
        BytesSent += Size;
        return true;
    }
    return false;
}

bool MServerConnection::SendServerPush(uint8 StatusCode, TByteArray&& Payload) {
    // wire format: [StatusCode:1B][Reserved:2B][Payload:N](最小 3 字节 + 可选负载)
    // 不带 CallId —— 因此对端的 ConsumeServerCall 不会匹配到它,自然不会当成回包处理。
    // 实际约定的 Payload 是 16 字节小端 [ActorId:8B][SequenceId:8B],供发起方定位并
    // 删除对应的 outbox 条目。
    TByteArray Pkt;
    Pkt.reserve(3 + Payload.size());
    Pkt.push_back(StatusCode);
    Pkt.push_back(0); // 保留字节
    Pkt.push_back(0); // 保留字节
    Pkt.insert(Pkt.end(), Payload.begin(), Payload.end());
    return SendPacket(static_cast<uint8>(EServerMessageType::MT_ServerPush), Pkt.data(), static_cast<uint32>(Pkt.size()));
}

// 每帧驱动:Disconnected 走重连分支;Connected/Authenticated 走收包 + 心跳分支;
// Connecting 期间什么都不做(建连是同步完成的,不会长时间停留在此态)。
void MServerConnection::Tick(float DeltaTime) {
    if (State == EConnectionState::Disconnected) {
        if (Config.Address.empty() || Config.Port == 0) {
            return;
        }

        // 按 ReconnectInterval 节流;每次尝试自增 ReconnectAttempts 便于观测
        ReconnectTimer += DeltaTime;
        if (ReconnectTimer >= ReconnectInterval) {
            ReconnectTimer = 0.0f;
            ++ReconnectAttempts;
            LOG_INFO("%s Attempting to reconnect...", LogPrefix.c_str());
            TryConnect();
        }
        return;
    }

    if (State != EConnectionState::Connected && State != EConnectionState::Authenticated) {
        return;
    }

    // 先把本轮能收的包全部收完,再考虑发心跳
    ProcessRecv();

    // 未认证连接只允许握手控制流，不能提前发送普通心跳或业务消息。
    if (State == EConnectionState::Authenticated) {
        HeartbeatTimer += DeltaTime;
        if (HeartbeatTimer >= HeartbeatInterval) {
            HeartbeatTimer = 0.0f;
            SendHeartbeat();
        }
    }
}

void MServerConnection::ProcessRecv() {
    if (!Transport) {
        return;
    }

    // ReceivePacket 每次解出一个完整逻辑包(粘包/半包由传输层处理);
    // 返回 false 表示本轮没有再完整的包了。
    TByteArray Packet;
    while (Transport->ReceivePacket(Packet)) {
        if (!Packet.empty()) {
            BytesReceived += static_cast<uint64>(Packet.size());
            // 首字节是包类型,其余是负载
            const uint8 PacketType = Packet[0];
            TByteArray  Payload(Packet.begin() + 1, Packet.end());
            HandlePacket(PacketType, Payload);
        }
    }

    if (Transport && !Transport->IsConnected()) {
        LOG_INFO("%s Connection closed by remote", LogPrefix.c_str());
        Disconnect();
    }
}

void MServerConnection::HandlePacket(uint8 PacketType, const TByteArray& Data) {
    // 目前没有需要在此层特殊处理的包类型(心跳/握手都由 RPC 层承载),
    // 所有入站包统一上抛给应用层,由上层按 PacketType 分发。
    switch (PacketType) {
    default: {
        // 转发给应用层
        if (OnMessageCallback) {
            OnMessageCallback(shared_from_this(), PacketType, Data);
        }
        break;
    }
    }
}

void MServerConnection::SendHandshake() {
    const char* ClassName = GetServerEndpointClassName(Config.ServerType);
    if (!ClassName) {
        LOG_WARN("%s No RPC handshake endpoint for server type %d", LogPrefix.c_str(), static_cast<int>(Config.ServerType));
        return;
    }

    // 必须有回包才可进入 Authenticated：普通 MT_RPC 仅证明本端写入了缓冲，
    // 不能证明对端收到了。CallServerFunction 会校验 FunctionId 与来源连接。
    const TSharedPtr<MServerConnection> Self = shared_from_this();
    auto Handshake = CallServerFunction<SEmptyServerMessage>(Self, ClassName, "Rpc_OnServerHandshake", LocalServerInfo.ServerId);
    Handshake.Then([WeakConnection = TWeakPtr<MServerConnection>(Self)](MFuture<TResult<SEmptyServerMessage, FAppError>> ResultFuture) mutable {
        const TSharedPtr<MServerConnection> Connection = WeakConnection.lock();
        if (!Connection || Connection->State != EConnectionState::Connected) {
            return;
        }

        const TResult<SEmptyServerMessage, FAppError>& Result = ResultFuture.Peek();
        if (Result.IsErr()) {
            LOG_WARN("%s Handshake failed: %s", Connection->LogPrefix.c_str(), Result.GetError().Code.c_str());
            Connection->Disconnect();
            return;
        }

        Connection->State = EConnectionState::Authenticated;
        Connection->ReconnectAttempts = 0;
        LOG_INFO("%s Handshake acknowledged; connection authenticated", Connection->LogPrefix.c_str());
        if (Connection->OnServerAuthenticatedCallback) {
            Connection->OnServerAuthenticatedCallback(Connection, Connection->GetRemoteServerInfo());
        }
    });
}

void MServerConnection::SendHeartbeat() {
    const char* ClassName = GetServerEndpointClassName(Config.ServerType);
    if (!ClassName) {
        return;
    }

    // 心跳走普通 MT_RPC,不关心对端是否回包(序号仅用于日志/排查)。
    MRpc::CallRemote(*this, ClassName, "Rpc_OnHeartbeat", ++HeartbeatSeq);
}
