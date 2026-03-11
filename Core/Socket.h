#pragma once

#include "NetCore.h"
#include "Common/Logger.h"
#include <cstring>

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET TSocketFd;
    constexpr SOCKET INVALID_SOCKET_FD = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    typedef int TSocketFd;
    constexpr int INVALID_SOCKET_FD = -1;
#endif

// 网络错误码
enum class ENetError
{
    None = 0,
    WouldBlock = 1,
    Disconnected = 2,
    InvalidPacket = 3,
    SendFailed = 4,
    ReceiveFailed = 5
};

// 网络连接接口
class INetConnection
{
public:
    virtual ~INetConnection() = default;
    virtual bool Send(const void* Data, uint32 Size) = 0;
    virtual bool Receive(void* Buffer, uint32 Size, uint32& BytesRead) = 0;
    virtual uint64 GetPlayerId() const = 0;
    virtual void SetPlayerId(uint64 Id) = 0;
    virtual TSocketFd GetSocketFd() const = 0;
    virtual void SetNonBlocking(bool bNonBlocking) = 0;
    virtual bool IsConnected() const = 0;
    virtual void Close() = 0;
};

// TCP连接实现
class MTcpConnection : public INetConnection
{
private:
    TSocketFd SocketFd;
    uint64 PlayerId;
    bool bConnected;
    TString RemoteAddress;
    uint16 RemotePort;
    
    // 接收缓冲区
    TByteArray RecvBuffer;
    TByteArray SendBuffer;
    static constexpr uint32 RECV_BUFFER_SIZE = 65535;
    static constexpr uint32 SEND_BUFFER_SIZE = 65535;

public:
    MTcpConnection(TSocketFd InSocketFd);
    virtual ~MTcpConnection();
    
    // INetConnection接口
    bool Send(const void* Data, uint32 Size) override;
    bool Receive(void* Buffer, uint32 Size, uint32& BytesRead) override;
    bool ReceivePacket(TArray& OutPacket);
    bool FlushSendBuffer();
    bool HasPendingSendData() const { return !SendBuffer.empty(); }
    uint64 GetPlayerId() const override { return PlayerId; }
    void SetPlayerId(uint64 Id) override { PlayerId = Id; }
    TSocketFd GetSocketFd() const override { return SocketFd; }
    void SetNonBlocking(bool bNonBlocking) override;
    bool IsConnected() const override { return bConnected; }
    void Close() override;
    
    // 获取远端地址
    const TString& GetRemoteAddress() const { return RemoteAddress; }
    uint16 GetRemotePort() const { return RemotePort; }
    
    // 处理接收到的数据（粘包处理）
    bool ProcessRecvBuffer(TArray& OutPacket);
};

// Socket封装类
class MSocket
{
public:
    // 平台初始化（Windows: WSAStartup；Unix: 无操作）
    static bool EnsureInit();

    // 创建TCP监听socket
    static TSocketFd CreateListenSocket(uint16 Port, int32 MaxBacklog = 128);
    
    // 创建非阻塞socket
    static TSocketFd CreateNonBlockingSocket();
    
    // 设置socket为非阻塞
    static bool SetNonBlocking(TSocketFd Fd, bool bNonBlocking);
    
    // 设置TCP_NODELAY
    static bool SetNoDelay(TSocketFd Fd, bool bNoDelay);
    
    // 接受新连接
    static TSocketFd Accept(TSocketFd ListenFd, TString& OutAddress, uint16& OutPort);
    
    // 关闭socket
    static void Close(TSocketFd Fd);

    // 最后 socket 错误与是否“会阻塞”
    static int GetLastError();
    static bool IsWouldBlock(int Error);
};
