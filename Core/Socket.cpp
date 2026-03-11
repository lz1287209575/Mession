#include "Socket.h"
#if !defined(_WIN32) && !defined(_WIN64) && !defined(WIN32) && !defined(WIN64)
#include <netinet/tcp.h>
#endif

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
namespace
{
    static bool EnsureWinsockInit()
    {
        static bool bDone = false;
        if (bDone)
        {
            return true;
        }
        WSADATA WsaData;
        if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
        {
            return false;
        }
        bDone = true;
        return true;
    }
    static bool IsValidSocket(TSocketFd Fd) { return Fd != INVALID_SOCKET; }
    static int SocketGetLastError() { return WSAGetLastError(); }
    static bool SocketIsWouldBlock(int Err) { return Err == WSAEWOULDBLOCK; }
    static void SocketClose(TSocketFd Fd) { if (IsValidSocket(Fd)) { closesocket(Fd); } }
}
bool MSocket::EnsureInit()
{
    return EnsureWinsockInit();
}
#else
namespace
{
    static bool EnsureWinsockInit() { return true; }
    static bool IsValidSocket(TSocketFd Fd) { return Fd >= 0; }
    static int SocketGetLastError() { return errno; }
    static bool SocketIsWouldBlock(int Err) { return Err == EWOULDBLOCK || Err == EAGAIN; }
    static void SocketClose(TSocketFd Fd) { if (Fd >= 0) { close(Fd); } }
}
bool MSocket::EnsureInit()
{
    return true;
}
#endif

// MTcpConnection implementation
MTcpConnection::MTcpConnection(TSocketFd InSocketFd)
    : SocketFd(InSocketFd), PlayerId(0), bConnected(true)
{
    (void)EnsureWinsockInit();
    sockaddr_in ClientAddr = {};
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    int AddrLen = sizeof(ClientAddr);
#else
    socklen_t AddrLen = sizeof(ClientAddr);
#endif
    if (getpeername(SocketFd, (sockaddr*)&ClientAddr, &AddrLen) == 0)
    {
        char AddrBuf[64];
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
        InetNtopA(AF_INET, &ClientAddr.sin_addr, AddrBuf, sizeof(AddrBuf));
        RemoteAddress = AddrBuf;
#else
        RemoteAddress = inet_ntoa(ClientAddr.sin_addr);
#endif
        RemotePort = ntohs(ClientAddr.sin_port);
    }
    else
    {
        RemoteAddress = "unknown";
        RemotePort = 0;
    }

    RecvBuffer.reserve(RECV_BUFFER_SIZE);

    LOG_INFO("New connection from %s:%d (fd=%zd)",
             RemoteAddress.c_str(), (int)RemotePort, (intptr_t)SocketFd);
}

MTcpConnection::~MTcpConnection()
{
    Close();
}

bool MTcpConnection::Send(const void* Data, uint32 Size)
{
    if (!bConnected || Size == 0)
    {
        return false;
    }

    if (Size > MAX_PACKET_SIZE)
    {
        LOG_ERROR("Packet too large to send: %u", Size);
        return false;
    }

    if (SendBuffer.size() + 4 + Size > SEND_BUFFER_SIZE)
    {
        LOG_ERROR("Send buffer overflow on fd=%zd", (intptr_t)SocketFd);
        bConnected = false;
        return false;
    }

    const size_t OldSize = SendBuffer.size();
    SendBuffer.resize(OldSize + 4 + Size);

    memcpy(SendBuffer.data() + OldSize, &Size, sizeof(Size));
    memcpy(SendBuffer.data() + OldSize + 4, Data, Size);

    return FlushSendBuffer();
}

bool MTcpConnection::Receive(void* Buffer, uint32 Size, uint32& BytesRead)
{
    TArray Packet;
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

bool MTcpConnection::ReceivePacket(TArray& OutPacket)
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
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
        int BytesRead = recv(SocketFd, (char*)Buffer, (int)sizeof(Buffer), 0);
#else
        ssize_t BytesRead = recv(SocketFd, Buffer, sizeof(Buffer), 0);
#endif

        if (BytesRead > 0)
        {
            if (RecvBuffer.size() + static_cast<size_t>(BytesRead) > RECV_BUFFER_SIZE)
            {
                LOG_ERROR("Receive buffer overflow on fd=%zd", (intptr_t)SocketFd);
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

        if (SocketIsWouldBlock(SocketGetLastError()))
        {
            return false;
        }

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
        LOG_ERROR("Receive failed: %d", SocketGetLastError());
#else
        LOG_ERROR("Receive failed: %s", strerror(errno));
#endif
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
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
        int Sent = send(SocketFd, (const char*)SendBuffer.data(), (int)SendBuffer.size(), 0);
#else
        ssize_t Sent = send(SocketFd, SendBuffer.data(), SendBuffer.size(), 0);
#endif

        if (Sent > 0)
        {
            SendBuffer.erase(SendBuffer.begin(), SendBuffer.begin() + Sent);
            continue;
        }

        if (Sent < 0 && SocketIsWouldBlock(SocketGetLastError()))
        {
            return true;
        }

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
        LOG_ERROR("Send failed: %d", SocketGetLastError());
#else
        LOG_ERROR("Send failed: %s", strerror(errno));
#endif
        bConnected = false;
        return false;
    }

    return true;
}

void MTcpConnection::SetNonBlocking(bool bNonBlocking)
{
    MSocket::SetNonBlocking(SocketFd, bNonBlocking);
}

void MTcpConnection::Close()
{
    if (bConnected)
    {
        LOG_DEBUG("Closing connection (player=%llu, fd=%zd)",
                  (unsigned long long)PlayerId, (intptr_t)SocketFd);
        MSocket::Close(SocketFd);
        bConnected = false;
    }

    RecvBuffer.clear();
    SendBuffer.clear();
}

bool MTcpConnection::ProcessRecvBuffer(TArray& OutPacket)
{
    // 简单的粘包处理：先读4字节长度头
    if (RecvBuffer.size() < 4)
    {
        return false;
    }

    uint32 PacketSize = 0;
    memcpy(&PacketSize, RecvBuffer.data(), sizeof(PacketSize));
    
    if (PacketSize == 0 || PacketSize > MAX_PACKET_SIZE)
    {
        LOG_ERROR("Invalid packet size: %u", PacketSize);
        bConnected = false;
        return false;
    }
    
    if (RecvBuffer.size() < 4 + PacketSize)
    {
        return false;
    }
    
    // 提取数据包
    OutPacket.assign(RecvBuffer.begin() + 4, RecvBuffer.begin() + 4 + PacketSize);
    
    // 移除已处理的数据
    RecvBuffer.erase(RecvBuffer.begin(), RecvBuffer.begin() + 4 + PacketSize);
    
    return true;
}

// MSocket implementation
TSocketFd MSocket::CreateListenSocket(uint16 Port, int32 MaxBacklog)
{
    if (!EnsureWinsockInit())
    {
        LOG_ERROR("WSAStartup failed");
        return INVALID_SOCKET_FD;
    }
    TSocketFd ListenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (!IsValidSocket(ListenFd))
    {
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
        LOG_ERROR("Failed to create socket: %d", SocketGetLastError());
#else
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
#endif
        return INVALID_SOCKET_FD;
    }

    int32 ReuseAddr = 1;
    setsockopt(ListenFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&ReuseAddr, sizeof(ReuseAddr));

    sockaddr_in Addr = {};
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons(Port);
    Addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(ListenFd, (sockaddr*)&Addr, sizeof(Addr)) != 0)
    {
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
        LOG_ERROR("Failed to bind port %d: %d", Port, SocketGetLastError());
#else
        LOG_ERROR("Failed to bind port %d: %s", Port, strerror(errno));
#endif
        Close(ListenFd);
        return INVALID_SOCKET_FD;
    }

    if (listen(ListenFd, MaxBacklog) != 0)
    {
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
        LOG_ERROR("Failed to listen: %d", SocketGetLastError());
#else
        LOG_ERROR("Failed to listen: %s", strerror(errno));
#endif
        Close(ListenFd);
        return INVALID_SOCKET_FD;
    }

    SetNonBlocking(ListenFd, true);

    LOG_INFO("Listening on port %d (fd=%zd)", Port, (intptr_t)ListenFd);
    return ListenFd;
}

TSocketFd MSocket::CreateNonBlockingSocket()
{
    if (!EnsureWinsockInit())
    {
        return INVALID_SOCKET_FD;
    }
    TSocketFd Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (IsValidSocket(Fd))
    {
        SetNonBlocking(Fd, true);
        SetNoDelay(Fd, true);
    }
    return Fd;
}

bool MSocket::SetNonBlocking(TSocketFd Fd, bool bNonBlocking)
{
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    u_long Mode = bNonBlocking ? 1 : 0;
    return ioctlsocket(Fd, FIONBIO, &Mode) == 0;
#else
    int32 Flags = fcntl(Fd, F_GETFL, 0);
    if (Flags < 0)
    {
        return false;
    }
    if (bNonBlocking)
    {
        Flags |= O_NONBLOCK;
    }
    else
    {
        Flags &= ~O_NONBLOCK;
    }
    return fcntl(Fd, F_SETFL, Flags) == 0;
#endif
}

bool MSocket::SetNoDelay(TSocketFd Fd, bool bNoDelay)
{
    int32 NoDelay = bNoDelay ? 1 : 0;
    return setsockopt(Fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&NoDelay, sizeof(NoDelay)) == 0;
}

TSocketFd MSocket::Accept(TSocketFd ListenFd, TString& OutAddress, uint16& OutPort)
{
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    int AddrLen = sizeof(sockaddr_in);
#else
    socklen_t AddrLen = sizeof(sockaddr_in);
#endif
    sockaddr_in ClientAddr = {};
    TSocketFd ClientFd = accept(ListenFd, (sockaddr*)&ClientAddr, &AddrLen);

    if (IsValidSocket(ClientFd))
    {
        char AddrBuf[64];
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
        InetNtopA(AF_INET, &ClientAddr.sin_addr, AddrBuf, sizeof(AddrBuf));
        OutAddress = AddrBuf;
#else
        OutAddress = inet_ntoa(ClientAddr.sin_addr);
#endif
        OutPort = ntohs(ClientAddr.sin_port);

        SetNonBlocking(ClientFd, true);
        SetNoDelay(ClientFd, true);
    }

    return ClientFd;
}

void MSocket::Close(TSocketFd Fd)
{
    SocketClose(Fd);
}

int MSocket::GetLastError()
{
    return SocketGetLastError();
}

bool MSocket::IsWouldBlock(int Error)
{
    return SocketIsWouldBlock(Error);
}
