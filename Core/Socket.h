#pragma once

#include "NetCore.h"
#include "SocketHandle.h"
#include "SocketPlatform.h"
#include "Common/Logger.h"
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

struct SAcceptedSocket
{
    MSocketHandle Socket;
    FString RemoteAddress;
    uint16 RemotePort = 0;

    bool IsValid() const
    {
        return Socket.IsValid();
    }
};

// 网络连接接口（客户端连接统一通过此抽象）
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

    // 包模式（粘包/半包处理），默认返回 false/空
    virtual bool ReceivePacket(TArray& OutPacket) { (void)OutPacket; return false; }
    virtual bool ProcessRecvBuffer(TArray& OutPacket) { (void)OutPacket; return false; }
    virtual bool FlushSendBuffer() { return true; }
    virtual bool HasPendingSendData() const { return false; }
};

// TCP连接实现
class MTcpConnection : public INetConnection
{
private:
    MSocketHandle Socket;
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
    MTcpConnection(MSocketHandle&& InSocket, const FString& InRemoteAddress = "", uint16 InRemotePort = 0);
    virtual ~MTcpConnection();

    static TSharedPtr<MTcpConnection> ConnectTo(const SSocketAddress& Address, float TimeoutSeconds);
    
    // INetConnection接口
    bool Send(const void* Data, uint32 Size) override;
    bool Receive(void* Buffer, uint32 Size, uint32& BytesRead) override;
    bool ReceivePacket(TArray& OutPacket) override;
    bool FlushSendBuffer() override;
    bool HasPendingSendData() const override { return !SendBuffer.empty(); }
    uint64 GetPlayerId() const override { return PlayerId; }
    void SetPlayerId(uint64 Id) override { PlayerId = Id; }
    TSocketFd GetSocketFd() const override { return Socket.Get(); }
    void SetNonBlocking(bool bNonBlocking) override;
    bool IsConnected() const override { return bConnected; }
    void Close() override;
    
    // 获取远端地址
    const TString& GetRemoteAddress() const { return RemoteAddress; }
    uint16 GetRemotePort() const { return RemotePort; }
    
    // 处理接收到的数据（粘包处理）
    bool ProcessRecvBuffer(TArray& OutPacket) override;
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
    static SAcceptedSocket AcceptConnection(TSocketFd ListenFd);
    
    // 关闭socket
    static void Close(TSocketFd Fd);

    // 最后 socket 错误与是否“会阻塞”
    static int GetLastError();
    static bool IsWouldBlock(int Error);
};
