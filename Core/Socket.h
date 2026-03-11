#pragma once

#include "NetCore.h"
#include "../Common/Logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

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
    virtual int32 GetSocketFd() const = 0;
    virtual void SetNonBlocking(bool bNonBlocking) = 0;
    virtual bool IsConnected() const = 0;
    virtual void Close() = 0;
};

// TCP连接实现
class MTcpConnection : public INetConnection
{
private:
    int32 SocketFd;
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
    MTcpConnection(int32 InSocketFd);
    virtual ~MTcpConnection();
    
    // INetConnection接口
    bool Send(const void* Data, uint32 Size) override;
    bool Receive(void* Buffer, uint32 Size, uint32& BytesRead) override;
    bool ReceivePacket(TArray& OutPacket);
    bool FlushSendBuffer();
    bool HasPendingSendData() const { return !SendBuffer.empty(); }
    uint64 GetPlayerId() const override { return PlayerId; }
    void SetPlayerId(uint64 Id) override { PlayerId = Id; }
    int32 GetSocketFd() const override { return SocketFd; }
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
    // 创建TCP监听socket
    static int32 CreateListenSocket(uint16 Port, int32 MaxBacklog = 128);
    
    // 创建非阻塞socket
    static int32 CreateNonBlockingSocket();
    
    // 设置socket为非阻塞
    static bool SetNonBlocking(int32 SocketFd, bool bNonBlocking);
    
    // 设置TCP_NODELAY
    static bool SetNoDelay(int32 SocketFd, bool bNoDelay);
    
    // 接受新连接
    static int32 Accept(int32 ListenSocketFd, TString& OutAddress, uint16& OutPort);
    
    // 关闭socket
    static void Close(int32 SocketFd);
};
