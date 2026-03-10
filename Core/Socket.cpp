#include "Socket.h"
#include <netinet/tcp.h>

// FTcpConnection implementation
FTcpConnection::FTcpConnection(int32 InSocketFd) 
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

FTcpConnection::~FTcpConnection()
{
    Close();
}

bool FTcpConnection::Send(const void* Data, uint32 Size)
{
    if (!bConnected || Size == 0)
        return false;
    
    // 添加4字节长度头
    uint8 Packet[MAX_PACKET_SIZE];
    *(uint32*)Packet = Size;
    memcpy(Packet + 4, Data, Size);
    
    int32 Sent = send(SocketFd, Packet, Size + 4, 0);
    
    if (Sent < 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            // 缓冲区满，稍后重试
            return false;
        }
        
        LOG_ERROR("Send failed: %s", strerror(errno));
        bConnected = false;
        return false;
    }
    
    return true;
}

bool FTcpConnection::Receive(void* Buffer, uint32 Size, uint32& BytesRead)
{
    if (!bConnected)
        return false;
    
    BytesRead = recv(SocketFd, Buffer, Size, 0);
    
    if (BytesRead > 0)
    {
        return true;
    }
    else if (BytesRead == 0)
    {
        // 连接关闭
        LOG_INFO("Connection closed by peer (player=%llu)", (unsigned long long)PlayerId);
        bConnected = false;
        return false;
    }
    else
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            // 没有数据
            return false;
        }
        
        LOG_ERROR("Receive failed: %s", strerror(errno));
        bConnected = false;
        return false;
    }
}

void FTcpConnection::SetNonBlocking(bool bNonBlocking)
{
    FSocket::SetNonBlocking(SocketFd, bNonBlocking);
}

void FTcpConnection::Close()
{
    if (bConnected)
    {
        LOG_DEBUG("Closing connection (player=%llu, fd=%d)", 
                  (unsigned long long)PlayerId, SocketFd);
        FSocket::Close(SocketFd);
        bConnected = false;
    }
}

bool FTcpConnection::ProcessRecvBuffer(TArray& OutPacket)
{
    // 简单的粘包处理：先读4字节长度头
    if (RecvBuffer.size() < 4)
        return false;
    
    uint32 PacketSize = *(uint32*)RecvBuffer.data();
    
    if (PacketSize > MAX_PACKET_SIZE)
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

// FSocket implementation
int32 FSocket::CreateListenSocket(uint16 Port, int32 MaxBacklog)
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

int32 FSocket::CreateNonBlockingSocket()
{
    int32 Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd >= 0)
    {
        SetNonBlocking(Fd, true);
        SetNoDelay(Fd, true);
    }
    return Fd;
}

bool FSocket::SetNonBlocking(int32 SocketFd, bool bNonBlocking)
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

bool FSocket::SetNoDelay(int32 SocketFd, bool bNoDelay)
{
    int32 NoDelay = bNoDelay ? 1 : 0;
    return setsockopt(SocketFd, IPPROTO_TCP, TCP_NODELAY, &NoDelay, sizeof(NoDelay)) == 0;
}

int32 FSocket::Accept(int32 ListenSocketFd, std::string& OutAddress, uint16& OutPort)
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

void FSocket::Close(int32 SocketFd)
{
    if (SocketFd >= 0)
    {
        close(SocketFd);
    }
}
