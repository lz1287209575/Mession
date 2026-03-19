#include "Socket.h"
#include "PacketCodec.h"
#include "Poll.h"

// MTcpConnection implementation
MTcpConnection::MTcpConnection(TSocketFd InSocketFd)
    : Socket(InSocketFd), PlayerId(0), bConnected(true)
{
    (void)MSocketPlatform::EnsureInit();
    MSocketPlatform::GetPeerAddress(Socket.Get(), RemoteAddress, RemotePort);

    RecvBuffer.reserve(RECV_BUFFER_SIZE);

    LOG_INFO("New connection from %s:%d (fd=%zd)",
             RemoteAddress.c_str(), (int)RemotePort, (intptr_t)Socket.Get());
}

MTcpConnection::MTcpConnection(MSocketHandle&& InSocket, const MString& InRemoteAddress, uint16 InRemotePort)
    : Socket(std::move(InSocket)), PlayerId(0), bConnected(Socket.IsValid()), RemoteAddress(InRemoteAddress), RemotePort(InRemotePort)
{
    (void)MSocketPlatform::EnsureInit();

    if (RemoteAddress.empty() && Socket.IsValid())
    {
        MSocketPlatform::GetPeerAddress(Socket.Get(), RemoteAddress, RemotePort);
    }

    RecvBuffer.reserve(RECV_BUFFER_SIZE);

    LOG_INFO("New connection from %s:%d (fd=%zd)",
             RemoteAddress.c_str(), (int)RemotePort, (intptr_t)Socket.Get());
}

MTcpConnection::~MTcpConnection()
{
    Close();
}

TSharedPtr<MTcpConnection> MTcpConnection::ConnectTo(const SSocketAddress& Address, float TimeoutSeconds)
{
    if (!MSocketPlatform::EnsureInit())
    {
        return nullptr;
    }

    TSocketFd NewSocketFd = MSocketPlatform::CreateTcpSocket();
    if (NewSocketFd == INVALID_SOCKET_FD)
    {
        return nullptr;
    }

    MSocketPlatform::SetNonBlocking(NewSocketFd, true);
    MSocketPlatform::SetNoDelay(NewSocketFd, true);

    sockaddr_in SockAddr = {};
    if (!Address.IsValid() || !MSocketPlatform::ParseIPv4Address(Address, SockAddr))
    {
        MSocketPlatform::CloseSocket(NewSocketFd);
        return nullptr;
    }

    const int Result = MSocketPlatform::Connect(NewSocketFd, SockAddr);
    const int LastError = MSocketPlatform::GetLastError();
    if (Result != 0 && !MSocketPlatform::IsConnectInProgress(LastError))
    {
        MSocketPlatform::CloseSocket(NewSocketFd);
        return nullptr;
    }

    pollfd PollFd;
    PollFd.fd = NewSocketFd;
    PollFd.events = POLLOUT;
    PollFd.revents = 0;

    const int PollResult = poll(&PollFd, 1, static_cast<int>(TimeoutSeconds * 1000.0f));
    if (PollResult <= 0 || !(PollFd.revents & POLLOUT))
    {
        MSocketPlatform::CloseSocket(NewSocketFd);
        return nullptr;
    }

    return MakeShared<MTcpConnection>(NewSocketFd);
}

bool MTcpConnection::Send(const void* Data, uint32 Size)
{
    if (!bConnected)
    {
        LOG_WARN("Attempted to send on disconnected connection (player=%llu, fd=%zd)",
                 (unsigned long long)PlayerId, (intptr_t)Socket.Get());
        return false;
    }

    if (Size == 0)
    {
        LOG_WARN("Attempted to send empty packet (player=%llu, fd=%zd)",
                 (unsigned long long)PlayerId, (intptr_t)Socket.Get());
        return false;
    }

    if (Size > MAX_PACKET_SIZE)
    {
        LOG_ERROR("Packet too large to send: %u", Size);
        return false;
    }

    TByteArray Payload;
    Payload.resize(Size);
    memcpy(Payload.data(), Data, Size);

    TByteArray EncodedPacket;
    if (!MLengthPrefixedPacketCodec::EncodePacket(Payload, EncodedPacket))
    {
        LOG_ERROR("Failed to encode packet for send: %u", Size);
        return false;
    }

    if (SendBuffer.size() + EncodedPacket.size() > SEND_BUFFER_SIZE)
    {
        LOG_ERROR("Send buffer overflow on fd=%zd (player=%llu, queued=%zu, incoming=%zu)",
                  (intptr_t)Socket.Get(),
                  (unsigned long long)PlayerId,
                  SendBuffer.size(),
                  EncodedPacket.size());
        bConnected = false;
        return false;
    }

    const size_t OldSize = SendBuffer.size();
    SendBuffer.resize(OldSize + EncodedPacket.size());
    memcpy(SendBuffer.data() + OldSize, EncodedPacket.data(), EncodedPacket.size());

    return FlushSendBuffer();
}

bool MTcpConnection::Receive(void* Buffer, uint32 Size, uint32& BytesRead)
{
    TByteArray Packet;
    if (!ReceivePacket(Packet))
    {
        BytesRead = 0;
        return false;
    }

    if (Packet.size() > Size)
    {
        LOG_ERROR("Receive buffer too small: packet=%zu buffer=%u", Packet.size(), Size);
        bConnected = false;
        BytesRead = 0;
        return false;
    }

    memcpy(Buffer, Packet.data(), Packet.size());
    BytesRead = static_cast<uint32>(Packet.size());
    return true;
}

bool MTcpConnection::ReceivePacket(TByteArray& OutPacket)
{
    if (!bConnected)
    {
        return false;
    }

    OutPacket.clear();
    FlushSendBuffer();

    while (bConnected)
    {
        if (ProcessRecvBuffer(OutPacket))
        {
            return true;
        }

        uint8 Buffer[8192];
        const int32 BytesRead = MSocketPlatform::Recv(Socket.Get(), Buffer, sizeof(Buffer));

        if (BytesRead > 0)
        {
            if (RecvBuffer.size() + static_cast<size_t>(BytesRead) > RECV_BUFFER_SIZE)
            {
                LOG_ERROR("Receive buffer overflow on fd=%zd", (intptr_t)Socket.Get());
                bConnected = false;
                return false;
            }

            RecvBuffer.insert(RecvBuffer.end(), Buffer, Buffer + BytesRead);
            continue;
        }

        if (BytesRead == 0)
        {
            LOG_INFO("Connection closed by peer (player=%llu)", (unsigned long long)PlayerId);
            bConnected = false;
            return false;
        }

        const int LastError = MSocketPlatform::GetLastError();
        if (MSocketPlatform::IsWouldBlock(LastError))
        {
            return false;
        }

        if (MSocketPlatform::IsConnectionReset(LastError))
        {
            LOG_INFO("Connection reset by peer (player=%llu)", (unsigned long long)PlayerId);
        }
        else
        {
            LOG_ERROR("Receive failed: %s", MSocketPlatform::GetLastErrorMessage().c_str());
        }
        bConnected = false;
        return false;
    }

    return false;
}

bool MTcpConnection::FlushSendBuffer()
{
    if (!bConnected)
    {
        return false;
    }

    while (!SendBuffer.empty())
    {
        const int32 Sent = MSocketPlatform::Send(Socket.Get(), SendBuffer.data(), static_cast<uint32>(SendBuffer.size()));

        if (Sent > 0)
        {
            SendBuffer.erase(SendBuffer.begin(), SendBuffer.begin() + Sent);
            continue;
        }

        if (Sent < 0 && MSocketPlatform::IsWouldBlock(MSocketPlatform::GetLastError()))
        {
            return true;
        }

        LOG_ERROR("Send failed: %s", MSocketPlatform::GetLastErrorMessage().c_str());
        bConnected = false;
        return false;
    }

    return true;
}

void MTcpConnection::SetNonBlocking(bool bNonBlocking)
{
    MSocket::SetNonBlocking(Socket.Get(), bNonBlocking);
}

void MTcpConnection::Close()
{
    if (bConnected)
    {
        LOG_DEBUG("Closing connection (player=%llu, fd=%zd)",
                  (unsigned long long)PlayerId, (intptr_t)Socket.Get());
        MSocketPlatform::ShutdownSocket(Socket.Get());
        Socket.Reset();
        bConnected = false;
    }

    RecvBuffer.clear();
    SendBuffer.clear();
}

bool MTcpConnection::ProcessRecvBuffer(TByteArray& OutPacket)
{
    const EPacketDecodeResult DecodeResult = MLengthPrefixedPacketCodec::TryDecodePacket(RecvBuffer, OutPacket);
    if (DecodeResult == EPacketDecodeResult::PacketReady)
    {
        return true;
    }

    if (DecodeResult == EPacketDecodeResult::InvalidPacket)
    {
        LOG_ERROR("Invalid packet in recv buffer on fd=%zd", (intptr_t)Socket.Get());
        bConnected = false;
    }

    return false;
}

// MSocket implementation
bool MSocket::EnsureInit()
{
    return MSocketPlatform::EnsureInit();
}

TSocketFd MSocket::CreateListenSocket(uint16 Port, int32 MaxBacklog)
{
    if (!MSocketPlatform::EnsureInit())
    {
        LOG_ERROR("Socket platform init failed");
        return INVALID_SOCKET_FD;
    }

    TSocketFd ListenFd = MSocketPlatform::CreateTcpSocket();
    if (ListenFd == INVALID_SOCKET_FD)
    {
        LOG_ERROR("Failed to create socket: %s", MSocketPlatform::GetLastErrorMessage().c_str());
        return INVALID_SOCKET_FD;
    }

    MSocketPlatform::SetReuseAddress(ListenFd, true);

    sockaddr_in Addr = {};
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons(Port);
    Addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(ListenFd, (sockaddr*)&Addr, sizeof(Addr)) != 0)
    {
        LOG_ERROR("Failed to bind port %d: %s", Port, MSocketPlatform::GetLastErrorMessage().c_str());
        Close(ListenFd);
        return INVALID_SOCKET_FD;
    }

    if (listen(ListenFd, MaxBacklog) != 0)
    {
        LOG_ERROR("Failed to listen: %s", MSocketPlatform::GetLastErrorMessage().c_str());
        Close(ListenFd);
        return INVALID_SOCKET_FD;
    }

    SetNonBlocking(ListenFd, true);

    return ListenFd;
}

TSocketFd MSocket::CreateNonBlockingSocket()
{
    if (!MSocketPlatform::EnsureInit())
    {
        return INVALID_SOCKET_FD;
    }

    TSocketFd Fd = MSocketPlatform::CreateTcpSocket();
    if (Fd != INVALID_SOCKET_FD)
    {
        SetNonBlocking(Fd, true);
        SetNoDelay(Fd, true);
    }
    return Fd;
}

bool MSocket::SetNonBlocking(TSocketFd Fd, bool bNonBlocking)
{
    return MSocketPlatform::SetNonBlocking(Fd, bNonBlocking);
}

bool MSocket::SetNoDelay(TSocketFd Fd, bool bNoDelay)
{
    return MSocketPlatform::SetNoDelay(Fd, bNoDelay);
}

TSocketFd MSocket::Accept(TSocketFd ListenFd, MString& OutAddress, uint16& OutPort)
{
    SAcceptedSocket Accepted = AcceptConnection(ListenFd);
    OutAddress = Accepted.RemoteAddress;
    OutPort = Accepted.RemotePort;
    return Accepted.Socket.Release();
}

SAcceptedSocket MSocket::AcceptConnection(TSocketFd ListenFd)
{
    SAcceptedSocket Result;

    sockaddr_in ClientAddr = {};
    TSocketFd ClientFd = MSocketPlatform::Accept(ListenFd, ClientAddr);
    if (ClientFd == INVALID_SOCKET_FD)
    {
        return Result;
    }

    Result.Socket.Reset(ClientFd);
    MSocketPlatform::DescribeAddress(ClientAddr, Result.RemoteAddress, Result.RemotePort);
    SetNonBlocking(Result.Socket.Get(), true);
    SetNoDelay(Result.Socket.Get(), true);
    return Result;
}

void MSocket::Close(TSocketFd Fd)
{
    MSocketPlatform::CloseSocket(Fd);
}

int MSocket::GetLastError()
{
    return MSocketPlatform::GetLastError();
}

bool MSocket::IsWouldBlock(int Error)
{
    return MSocketPlatform::IsWouldBlock(Error);
}
