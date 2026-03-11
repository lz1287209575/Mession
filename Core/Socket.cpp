#include "Socket.h"
#include <netinet/tcp.h>

// MTcpConnection implementation
MTcpConnection::MTcpConnection(int32 InSocketFd) 
    : SocketFd(InSocketFd), PlayerId(0), bConnected(true)
{
    // 获取远端地址
    sockaddr_in ClientAddr;
    socklen_t AddrLen = sizeof(ClientAddr);
    
    if (getpeername(SocketFd, (sockaddr*)&ClientAddr, &AddrLen) == 0)
    {
        RemoteAddress = inet_ntoa(ClientAddr.sin_addr);
        RemotePort = ntohs(ClientAddr.sin_port);
    }
    else
    {
        RemoteAddress = "unknown";
        RemotePort = 0;
    }
    
    RecvBuffer.reserve(RECV_BUFFER_SIZE);
    
    LOG_INFO("New connection from %s:%d (fd=%d)", 
             RemoteAddress.c_str(), RemotePort, SocketFd);
}

MTcpConnection::~MTcpConnection()
{
    Close();
}

bool MTcpConnection::Send(const void* Data, uint32 Size)
{
    if (!bConnected || Size == 0)
        return false;

    if (Size > MAX_PACKET_SIZE)
    {
        LOG_ERROR("Packet too large to send: %u", Size);
        return false;
    }

    if (SendBuffer.size() + 4 + Size > SEND_BUFFER_SIZE)
    {
        LOG_ERROR("Send buffer overflow on fd=%d", SocketFd);
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
        return false;

    OutPacket.clear();
    FlushSendBuffer();

    while (bConnected)
    {
        if (ProcessRecvBuffer(OutPacket))
            return true;

        uint8 Buffer[8192];
        ssize_t BytesRead = recv(SocketFd, Buffer, sizeof(Buffer), 0);

        if (BytesRead > 0)
        {
            if (RecvBuffer.size() + static_cast<size_t>(BytesRead) > RECV_BUFFER_SIZE)
            {
                LOG_ERROR("Receive buffer overflow on fd=%d", SocketFd);
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

        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return false;

        LOG_ERROR("Receive failed: %s", strerror(errno));
        bConnected = false;
        return false;
    }

    return false;
}

bool MTcpConnection::FlushSendBuffer()
{
    if (!bConnected)
        return false;

    while (!SendBuffer.empty())
    {
        ssize_t Sent = send(SocketFd, SendBuffer.data(), SendBuffer.size(), 0);

        if (Sent > 0)
        {
            SendBuffer.erase(SendBuffer.begin(), SendBuffer.begin() + Sent);
            continue;
        }

        if (Sent < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
            return true;

        LOG_ERROR("Send failed: %s", strerror(errno));
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
        LOG_DEBUG("Closing connection (player=%llu, fd=%d)", 
                  (unsigned long long)PlayerId, SocketFd);
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
        return false;

    uint32 PacketSize = 0;
    memcpy(&PacketSize, RecvBuffer.data(), sizeof(PacketSize));
    
    if (PacketSize == 0 || PacketSize > MAX_PACKET_SIZE)
    {
        LOG_ERROR("Invalid packet size: %u", PacketSize);
        bConnected = false;
        return false;
    }
    
    if (RecvBuffer.size() < 4 + PacketSize)
        return false;
    
    // 提取数据包
    OutPacket.assign(RecvBuffer.begin() + 4, RecvBuffer.begin() + 4 + PacketSize);
    
    // 移除已处理的数据
    RecvBuffer.erase(RecvBuffer.begin(), RecvBuffer.begin() + 4 + PacketSize);
    
    return true;
}

// MSocket implementation
int32 MSocket::CreateListenSocket(uint16 Port, int32 MaxBacklog)
{
    int32 ListenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (ListenFd < 0)
    {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    // 设置SO_REUSEADDR
    int32 ReuseAddr = 1;
    setsockopt(ListenFd, SOL_SOCKET, SO_REUSEADDR, &ReuseAddr, sizeof(ReuseAddr));
    
    // 绑定地址
    sockaddr_in Addr = {};
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons(Port);
    Addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(ListenFd, (sockaddr*)&Addr, sizeof(Addr)) < 0)
    {
        LOG_ERROR("Failed to bind port %d: %s", Port, strerror(errno));
        Close(ListenFd);
        return -1;
    }
    
    // 监听
    if (listen(ListenFd, MaxBacklog) < 0)
    {
        LOG_ERROR("Failed to listen: %s", strerror(errno));
        Close(ListenFd);
        return -1;
    }
    
    // 设置为非阻塞
    SetNonBlocking(ListenFd, true);
    
    LOG_INFO("Listening on port %d (fd=%d)", Port, ListenFd);
    return ListenFd;
}

int32 MSocket::CreateNonBlockingSocket()
{
    int32 Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd >= 0)
    {
        SetNonBlocking(Fd, true);
        SetNoDelay(Fd, true);
    }
    return Fd;
}

bool MSocket::SetNonBlocking(int32 SocketFd, bool bNonBlocking)
{
    int32 Flags = fcntl(SocketFd, F_GETFL, 0);
    if (Flags < 0)
        return false;
    
    if (bNonBlocking)
        Flags |= O_NONBLOCK;
    else
        Flags &= ~O_NONBLOCK;
    
    return fcntl(SocketFd, F_SETFL, Flags) == 0;
}

bool MSocket::SetNoDelay(int32 SocketFd, bool bNoDelay)
{
    int32 NoDelay = bNoDelay ? 1 : 0;
    return setsockopt(SocketFd, IPPROTO_TCP, TCP_NODELAY, &NoDelay, sizeof(NoDelay)) == 0;
}

int32 MSocket::Accept(int32 ListenSocketFd, TString& OutAddress, uint16& OutPort)
{
    sockaddr_in ClientAddr;
    socklen_t AddrLen = sizeof(ClientAddr);
    
    int32 ClientFd = accept(ListenSocketFd, (sockaddr*)&ClientAddr, &AddrLen);
    
    if (ClientFd >= 0)
    {
        OutAddress = inet_ntoa(ClientAddr.sin_addr);
        OutPort = ntohs(ClientAddr.sin_port);
        
        // 新连接设为非阻塞
        SetNonBlocking(ClientFd, true);
        SetNoDelay(ClientFd, true);
    }
    
    return ClientFd;
}

void MSocket::Close(int32 SocketFd)
{
    if (SocketFd >= 0)
    {
        close(SocketFd);
    }
}
