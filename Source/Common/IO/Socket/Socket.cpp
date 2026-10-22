/**
 * @file Socket.cpp
 * @brief Socket.h 的实现:MTcpConnection 的收发/粘包/断开判定,以及 MSocket 的工厂方法。
 *
 * @detail 本文件是"非阻塞 + 长度前缀"协议语义的唯一落脚点:
 * 所有失败路径都会显式把 bConnected 置 false(见 Send/ReceivePacket/ProcessRecvBuffer),
 * 因此调用方只需在每帧后检查 IsConnected() 就能发现被断开的连接。
 */
#include "Common/IO/Socket/Socket.h"
#include "Common/IO/Socket/Poll.h"
#include "Common/Serialization/PacketCodec.h"

// ---------------------------------------------------------------------------
// MTcpConnection 实现
// ---------------------------------------------------------------------------

MTcpConnection::MTcpConnection(TSocketFd InSocketFd) : Socket(InSocketFd), PlayerId(0), bConnected(true) {
    (void)MSocketPlatform::EnsureInit();
    MSocketPlatform::GetPeerAddress(Socket.Get(), RemoteAddress, RemotePort);

    RecvBuffer.reserve(RECV_BUFFER_SIZE);

    LOG_INFO("New connection from %s:%d (fd=%zd)", RemoteAddress.c_str(), (int)RemotePort, (intptr_t)Socket.Get());
}

MTcpConnection::MTcpConnection(MSocketHandle&& InSocket, const MString& InRemoteAddress, uint16 InRemotePort)
    : Socket(std::move(InSocket)), PlayerId(0), bConnected(Socket.IsValid()), RemoteAddress(InRemoteAddress), RemotePort(InRemotePort) {
    (void)MSocketPlatform::EnsureInit();

    // 调用方没给地址(常见于直接接管 fd)才去问系统,避免多一次 getpeername
    if (RemoteAddress.empty() && Socket.IsValid()) {
        MSocketPlatform::GetPeerAddress(Socket.Get(), RemoteAddress, RemotePort);
    }

    RecvBuffer.reserve(RECV_BUFFER_SIZE);

    LOG_INFO("New connection from %s:%d (fd=%zd)", RemoteAddress.c_str(), (int)RemotePort, (intptr_t)Socket.Get());
}

MTcpConnection::~MTcpConnection() {
    Close();
}

TSharedPtr<MTcpConnection> MTcpConnection::ConnectTo(const SSocketAddress& Address, float TimeoutSeconds) {
    if (!MSocketPlatform::EnsureInit()) {
        return nullptr;
    }

    TSocketFd NewSocketFd = MSocketPlatform::CreateTcpSocket();
    if (NewSocketFd == INVALID_SOCKET_FD) {
        return nullptr;
    }

    // 先转非阻塞:connect 立即返回,由后面的 poll 等它完成
    MSocketPlatform::SetNonBlocking(NewSocketFd, true);
    MSocketPlatform::SetNoDelay(NewSocketFd, true);

    sockaddr_in SockAddr = {};
    if (!Address.IsValid() || !MSocketPlatform::ParseIPv4Address(Address, SockAddr)) {
        MSocketPlatform::CloseSocket(NewSocketFd);
        return nullptr;
    }

    // 非阻塞 connect:返回非 0 但错误是"进行中"时属正常,继续等 poll
    const int Result    = MSocketPlatform::Connect(NewSocketFd, SockAddr);
    const int LastError = MSocketPlatform::GetLastError();
    if (Result != 0 && !MSocketPlatform::IsConnectInProgress(LastError)) {
        MSocketPlatform::CloseSocket(NewSocketFd);
        return nullptr;
    }

    pollfd PollFd;
    PollFd.fd      = NewSocketFd;
    PollFd.events  = POLLOUT; // 可写 = connect 已完成(失败也会先变成可写,故需 SO_ERROR 才能严格判定)
    PollFd.revents = 0;

    const int PollResult = poll(&PollFd, 1, static_cast<int>(TimeoutSeconds * 1000.0f));
    if (PollResult <= 0 || !(PollFd.revents & POLLOUT) || MSocketPlatform::GetSocketError(NewSocketFd) != 0) {
        MSocketPlatform::CloseSocket(NewSocketFd);
        return nullptr;
    }

    // 用裸 fd 构造:连接建立后对端地址由构造函数里的 getpeername 补齐
    return MakeShared<MTcpConnection>(NewSocketFd);
}

bool MTcpConnection::Send(const void* Data, uint32 Size) {
    if (!bConnected) {
        LOG_WARN("Attempted to send on disconnected connection (player=%llu, fd=%zd)", (unsigned long long)PlayerId, (intptr_t)Socket.Get());
        return false;
    }

    if (Size == 0) {
        // 空包会破坏长度前缀协议(解包端把 0 长度视为非法包),这里直接拒绝
        LOG_WARN("Attempted to send empty packet (player=%llu, fd=%zd)", (unsigned long long)PlayerId, (intptr_t)Socket.Get());
        return false;
    }

    if (Size > MAX_PACKET_SIZE) {
        LOG_ERROR("Packet too large to send: %u", Size);
        return false;
    }

    TByteArray Payload;
    Payload.resize(Size);
    memcpy(Payload.data(), Data, Size);

    // 加 4 字节长度前缀;编包与入队分两步,便于失败时回滚(此处未入队即返回)
    TByteArray EncodedPacket;
    if (!MLengthPrefixedPacketCodec::EncodePacket(Payload, EncodedPacket)) {
        LOG_ERROR("Failed to encode packet for send: %u", Size);
        return false;
    }

    // 发送队列有上限:超出即认为对端消费不过来(或已异常),直接断连而不是丢包。
    // 注意 MAX_PACKET_SIZE == SEND_BUFFER_SIZE == 65535,加上 4 字节前缀后,
    // 接近上限的大包必然踩到这里。
    if (SendBuffer.size() + EncodedPacket.size() > SEND_BUFFER_SIZE) {
        LOG_ERROR("Send buffer overflow on fd=%zd (player=%llu, queued=%zu, incoming=%zu)", (intptr_t)Socket.Get(), (unsigned long long)PlayerId, SendBuffer.size(), EncodedPacket.size());
        bConnected = false;
        return false;
    }

    const size_t OldSize = SendBuffer.size();
    SendBuffer.resize(OldSize + EncodedPacket.size());
    memcpy(SendBuffer.data() + OldSize, EncodedPacket.data(), EncodedPacket.size());

    return FlushSendBuffer();
}

bool MTcpConnection::Receive(void* Buffer, uint32 Size, uint32& BytesRead) {
    TByteArray Packet;
    if (!ReceivePacket(Packet)) {
        BytesRead = 0;
        return false;
    }

    // 调用方缓冲装不下整包,属于调用方容量规划错误:按协议错误断连,避免半包残留
    if (Packet.size() > Size) {
        LOG_ERROR("Receive buffer too small: packet=%zu buffer=%u", Packet.size(), Size);
        bConnected = false;
        BytesRead  = 0;
        return false;
    }

    memcpy(Buffer, Packet.data(), Packet.size());
    BytesRead = static_cast<uint32>(Packet.size());
    return true;
}

bool MTcpConnection::ReceivePacket(TByteArray& OutPacket) {
    if (!bConnected) {
        return false;
    }

    OutPacket.clear();
    FlushSendBuffer(); // 每帧收包时顺手把积压的发出去

    while (bConnected) {
        // 半包已在缓冲里时,先试解不读 socket,省一次系统调用
        if (ProcessRecvBuffer(OutPacket)) {
            return true;
        }

        uint8       Buffer[8192]; // 栈上小缓冲:一次最多收 8KiB,多轮读完
        const int32 BytesRead = MSocketPlatform::Recv(Socket.Get(), Buffer, sizeof(Buffer));

        if (BytesRead > 0) {
            if (RecvBuffer.size() + static_cast<size_t>(BytesRead) > RECV_BUFFER_SIZE) {
                // 内层 while 是"读干 socket"循环,不会无限增长;读到超限说明对端持续灌数据
                LOG_ERROR("Receive buffer overflow on fd=%zd", (intptr_t)Socket.Get());
                bConnected = false;
                return false;
            }

            RecvBuffer.insert(RecvBuffer.end(), Buffer, Buffer + BytesRead);
            continue;
        }

        if (BytesRead == 0) {
            // recv 返回 0 = 对端已正常关闭(FIN)
            LOG_INFO("Connection closed by peer (player=%llu)", (unsigned long long)PlayerId);
            bConnected = false;
            return false;
        }

        const int LastError = MSocketPlatform::GetLastError();
        if (MSocketPlatform::IsWouldBlock(LastError)) {
            // 本轮没数据了:保持连接,等下次事件/下一帧
            return false;
        }

        if (MSocketPlatform::IsConnectionReset(LastError)) {
            LOG_INFO("Connection reset by peer (player=%llu)", (unsigned long long)PlayerId);
        } else {
            LOG_ERROR("Receive failed: %s", MSocketPlatform::GetLastErrorMessage().c_str());
        }
        bConnected = false;
        return false;
    }

    return false;
}

bool MTcpConnection::FlushSendBuffer() {
    if (!bConnected) {
        return false;
    }

    while (!SendBuffer.empty()) {
        const int32 Sent = MSocketPlatform::Send(Socket.Get(), SendBuffer.data(), static_cast<uint32>(SendBuffer.size()));

        if (Sent > 0) {
            // 可能只发了一部分:按返回值推进,下次调用续发剩余
            SendBuffer.erase(SendBuffer.begin(), SendBuffer.begin() + Sent);
            continue;
        }

        if (Sent < 0 && MSocketPlatform::IsWouldBlock(MSocketPlatform::GetLastError())) {
            // 内核发送缓冲已满:不算错误,保留剩余数据等下次冲
            return true;
        }

        LOG_ERROR("Send failed: %s", MSocketPlatform::GetLastErrorMessage().c_str());
        bConnected = false;
        return false;
    }

    return true;
}

void MTcpConnection::SetNonBlocking(bool bNonBlocking) {
    MSocket::SetNonBlocking(Socket.Get(), bNonBlocking);
}

void MTcpConnection::Close() {
    if (bConnected) {
        LOG_DEBUG("Closing connection (player=%llu, fd=%zd)", (unsigned long long)PlayerId, (intptr_t)Socket.Get());
        // 先 shutdown 让对方立刻收到 FIN,再由句柄的 Reset 释放 fd;顺序不能反
        MSocketPlatform::ShutdownSocket(Socket.Get());
        Socket.Reset();
        bConnected = false;
    }

    // 幂等:重复调用只会走到这里
    RecvBuffer.clear();
    SendBuffer.clear();
}

bool MTcpConnection::ProcessRecvBuffer(TByteArray& OutPacket) {
    const EPacketDecodeResult DecodeResult = MLengthPrefixedPacketCodec::TryDecodePacket(RecvBuffer, OutPacket);
    if (DecodeResult == EPacketDecodeResult::PacketReady) {
        return true;
    }

    if (DecodeResult == EPacketDecodeResult::InvalidPacket) {
        // 长度前缀非法(0 或超过 MAX_PACKET_SIZE):流已不可信,整条连接作废
        LOG_ERROR("Invalid packet in recv buffer on fd=%zd", (intptr_t)Socket.Get());
        bConnected = false;
    }

    return false;
}

// ---------------------------------------------------------------------------
// MSocket 实现
// ---------------------------------------------------------------------------

bool MSocket::EnsureInit() {
    return MSocketPlatform::EnsureInit();
}

TSocketFd MSocket::CreateListenSocket(uint16 Port, int32 MaxBacklog) {
    if (!MSocketPlatform::EnsureInit()) {
        LOG_ERROR("Socket platform init failed");
        return INVALID_SOCKET_FD;
    }

    TSocketFd ListenFd = MSocketPlatform::CreateTcpSocket();
    if (ListenFd == INVALID_SOCKET_FD) {
        LOG_ERROR("Failed to create socket: %s", MSocketPlatform::GetLastErrorMessage().c_str());
        return INVALID_SOCKET_FD;
    }

    // 必须在 bind 之前设置,否则来不及阻止 TIME_WAIT 导致的 bind 失败
    MSocketPlatform::SetReuseAddress(ListenFd, true);

    sockaddr_in Addr     = {};
    Addr.sin_family      = AF_INET;
    Addr.sin_port        = htons(Port);
    Addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(ListenFd, (sockaddr*)&Addr, sizeof(Addr)) != 0) {
        LOG_ERROR("Failed to bind port %d: %s", Port, MSocketPlatform::GetLastErrorMessage().c_str());
        Close(ListenFd);
        return INVALID_SOCKET_FD;
    }

    if (listen(ListenFd, MaxBacklog) != 0) {
        LOG_ERROR("Failed to listen: %s", MSocketPlatform::GetLastErrorMessage().c_str());
        Close(ListenFd);
        return INVALID_SOCKET_FD;
    }

    // 最后一步才转非阻塞:bind/listen 期间保持阻塞更省事
    SetNonBlocking(ListenFd, true);

    return ListenFd;
}

TSocketFd MSocket::CreateNonBlockingSocket() {
    if (!MSocketPlatform::EnsureInit()) {
        return INVALID_SOCKET_FD;
    }

    TSocketFd Fd = MSocketPlatform::CreateTcpSocket();
    if (Fd != INVALID_SOCKET_FD) {
        SetNonBlocking(Fd, true);
        SetNoDelay(Fd, true);
    }
    return Fd;
}

bool MSocket::SetNonBlocking(TSocketFd Fd, bool bNonBlocking) {
    return MSocketPlatform::SetNonBlocking(Fd, bNonBlocking);
}

bool MSocket::SetNoDelay(TSocketFd Fd, bool bNoDelay) {
    return MSocketPlatform::SetNoDelay(Fd, bNoDelay);
}

TSocketFd MSocket::Accept(TSocketFd ListenFd, MString& OutAddress, uint16& OutPort) {
    SAcceptedSocket Accepted = AcceptConnection(ListenFd);
    OutAddress               = Accepted.RemoteAddress;
    OutPort                  = Accepted.RemotePort;
    return Accepted.Socket.Release(); // 交出所有权:调用方负责关闭
}

SAcceptedSocket MSocket::AcceptConnection(TSocketFd ListenFd) {
    SAcceptedSocket Result;

    sockaddr_in ClientAddr = {};
    TSocketFd   ClientFd   = MSocketPlatform::Accept(ListenFd, ClientAddr);
    if (ClientFd == INVALID_SOCKET_FD) {
        return Result; // 空结果:非阻塞监听 fd 上"暂无新连接"也走这里
    }

    Result.Socket.Reset(ClientFd);
    MSocketPlatform::DescribeAddress(ClientAddr, Result.RemoteAddress, Result.RemotePort);
    // 新连接一律非阻塞 + 关 Nagle,与项目其余连接保持一致的收发语义
    SetNonBlocking(Result.Socket.Get(), true);
    SetNoDelay(Result.Socket.Get(), true);
    return Result;
}

void MSocket::Close(TSocketFd Fd) {
    MSocketPlatform::CloseSocket(Fd);
}

int MSocket::GetLastError() {
    return MSocketPlatform::GetLastError();
}

bool MSocket::IsWouldBlock(int Error) {
    return MSocketPlatform::IsWouldBlock(Error);
}
