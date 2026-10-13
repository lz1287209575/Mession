/**
 * @file Socket.h
 * @brief TCP 连接抽象(INetConnection / MTcpConnection)与 socket 便捷工厂(MSocket)。
 *
 * @detail 数据面契约:
 *   - 全链路非阻塞:收/发遇到 WouldBlock 不算错误,没发完的数据留在 SendBuffer,
 *     由下一次 FlushSendBuffer() / ReceivePacket() 续发;
 *   - 收发走 4 字节长度前缀粘包(MLengthPrefixedPacketCodec):Receive() 一次返回
 *     一个完整逻辑包,半包留在 RecvBuffer 里等后续数据;
 *   - 连接状态集中在 bConnected 这一个 bool:对端断开、收/发缓冲溢出、解包失败都会
 *     把它置 false,之后 Send/Receive 立即失败,不再发系统调用。
 * 生命周期:fd 由成员 MSocketHandle 独占,析构即 close;本层不含重连逻辑。
 */
#pragma once

#include "Common/IO/Socket/SocketHandle.h"
#include "Common/IO/Socket/SocketPlatform.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/MLib.h"
#include <cstring>

/**
 * @brief ENetError - 网络错误分类枚举。
 *
 * @detail 目前是预留定义:仓库内没有任何使用点,连接层实际用 bool 返回值 +
 * MSocketPlatform::GetLastError() 判错。需要细分错误时再用,不要用它掩盖真实错误码。
 */
enum class ENetError { None = 0, WouldBlock = 1, Disconnected = 2, InvalidPacket = 3, SendFailed = 4, ReceiveFailed = 5 };

/**
 * @brief SAcceptedSocket - accept 的结果:连接句柄 + 对端地址。
 *
 * @detail 内含 MSocketHandle,因此不可拷贝、只可移动;若调用方没取走 Socket 就析构,
 * 连接会被自动关闭(Accept() 的裸 fd 版本正是通过 Release() 交出所有权)。
 */
struct SAcceptedSocket {
    MSocketHandle Socket;
    MString       RemoteAddress;
    uint16        RemotePort = 0;

    /**
     * @brief IsValid - accept 是否成功。
     * @return true 当且仅当底层 fd 有效
     */
    bool IsValid() const {
        return Socket.IsValid();
    }
};

/**
 * @brief INetConnection - 连接的统一抽象(客户端连接与服务器间连接共用)。
 *
 * @detail 纯虚部分是必需契约:发送、接收、fd、非阻塞模式、连接状态、关闭。
 * 粘包与发送缓冲相关方法给出默认实现(表示"本实现不支持分包/无发送队列"),
 * 只做整包收发的实现可以不覆盖。
 */
class INetConnection {
    public:
    virtual ~INetConnection()                                               = default;
    virtual bool      Send(const void* Data, uint32 Size)                   = 0;
    virtual bool      Receive(void* Buffer, uint32 Size, uint32& BytesRead) = 0;
    virtual uint64    GetPlayerId() const                                   = 0;
    virtual void      SetPlayerId(uint64 Id)                                = 0;
    virtual TSocketFd GetSocketFd() const                                   = 0;
    virtual void      SetNonBlocking(bool bNonBlocking)                     = 0;
    virtual bool      IsConnected() const                                   = 0;
    virtual void      Close()                                               = 0;

    /**
     * @brief GetRemoteAddress - 对端地址(形如 "127.0.0.1")。
     * @return 默认实现返回一个静态空串;有地址信息的实现应覆盖
     */
    virtual const MString& GetRemoteAddress() const {
        static const MString Empty;
        return Empty;
    }

    // 包模式(粘包/半包处理)相关:默认实现表示不支持分包
    /**
     * @brief ReceivePacket - 收一个完整逻辑包(粘包/半包由实现内部处理)。
     * @param OutPacket 输出包体(不含长度前缀)
     * @return true 收到一个完整包;false 暂无完整包、或连接已断
     *
     * @detail 默认实现只清空 OutPacket 并返回 false。
     */
    virtual bool ReceivePacket(TByteArray& OutPacket) {
        (void)OutPacket;
        return false;
    }

    /**
     * @brief ProcessRecvBuffer - 只尝试从现有缓冲解出一个包,不读 socket。
     * @param OutPacket 输出包体
     * @return true 解出完整包
     */
    virtual bool ProcessRecvBuffer(TByteArray& OutPacket) {
        (void)OutPacket;
        return false;
    }

    /**
     * @brief FlushSendBuffer - 尽量把发送缓冲写到 socket。
     * @return true 已发完,或被 WouldBlock 暂时挡住(可重试);false 发送出错
     *
     * @detail 默认实现无发送队列,恒返回 true。
     */
    virtual bool FlushSendBuffer() {
        return true;
    }

    /**
     * @brief HasPendingSendData - 是否还有未发出的数据。
     * @return 默认实现恒 false
     */
    virtual bool HasPendingSendData() const {
        return false;
    }
};

/**
 * @brief MTcpConnection - TCP 连接实现(非阻塞 + 4 字节长度前缀粘包 + 发送队列)。
 *
 * @detail
 *   - 线程模型:非线程安全;读写假定在同一 loop 线程,跨线程使用需外部串行化;
 *   - Send() 只把数据编包后入队并尝试写出,返回 true 只表示"已入队/已尽力发出",
 *     不代表对端已收到;可靠投递语义由上层负责;
 *   - RecvBuffer / SendBuffer 上限都是 65535 字节,一旦超出即判定对端异常或自身积压,
 *     直接把 bConnected 置 false(不重试、不丢弃最旧数据);
 *   - 因此单次 Send 的实际可用上限不是 MAX_PACKET_SIZE(65535),而是
 *     SEND_BUFFER_SIZE - 4 - 当前 SendBuffer.size()(见 Send 的说明);
 *   - 构造函数会写一条 INFO 日志(远端地址 + fd)。
 */
class MTcpConnection : public INetConnection {
    private:
    MSocketHandle Socket;
    uint64        PlayerId; // 业务侧绑定的玩家 ID(0 = 未绑定);本层只存不解释
    bool          bConnected;
    MString       RemoteAddress;
    uint16        RemotePort;

    // 收/发缓冲:RecvBuffer 攒半包,凑够一个完整包后交给调用方消费
    TByteArray              RecvBuffer;
    TByteArray              SendBuffer;
    static constexpr uint32 RECV_BUFFER_SIZE = 65535;
    static constexpr uint32 SEND_BUFFER_SIZE = 65535;

    public:
    // 接管一个已连接的 fd(所有权转移);对端地址由 getpeername 补齐
    MTcpConnection(TSocketFd InSocketFd);
    // 接管句柄并可显式指定对端地址;InRemoteAddress 为空时回退到 getpeername
    MTcpConnection(MSocketHandle&& InSocket, const MString& InRemoteAddress = "", uint16 InRemotePort = 0);
    virtual ~MTcpConnection();

    /**
     * @brief ConnectTo - 主动连接远端(非阻塞 connect + poll 等待完成)。
     * @param Address        目标 IPv4 地址(端口为主机字节序)
     * @param TimeoutSeconds 连接完成的等待上限(秒);内部换算成毫秒传给 poll
     * @return 已连接的 MTcpConnection;地址非法、connect 失败、超时、poll 出错都返回 nullptr
     *
     * @detail 已知局限:等待阶段只看 fd 可写(POLLOUT),没有用 SO_ERROR 二次确认,
     * 因此连接被立刻 RST 的极端情形也可能被判定为成功。
     */
    static TSharedPtr<MTcpConnection> ConnectTo(const SSocketAddress& Address, float TimeoutSeconds);

    // INetConnection接口
    /**
     * @brief Send - 编包入队并尝试立刻发出。
     * @param Data 数据首地址
     * @param Size 字节数
     * @return true 已入队(可能仍有残留未发出,见 HasPendingSendData)
     *
     * @detail 失败并返回 false 的几种情况:bConnected 已为 false、Size == 0、
     * Size > MAX_PACKET_SIZE(65535)、编包失败、或缓冲区溢出。
     * 注意 SEND_BUFFER_SIZE 与 MAX_PACKET_SIZE 同为 65535,而编包会额外加 4 字节
     * 长度前缀 —— 所以 Size 落在 65532..65535 时必然触发"发送缓冲溢出"分支,
     * 该分支会把连接置为已断开(bConnected = false),而不是丢弃这个包。
     */
    bool Send(const void* Data, uint32 Size) override;

    /**
     * @brief Receive - 收一个完整逻辑包。
     * @param Buffer   输出缓冲
     * @param Size     Buffer 的容量
     * @param BytesRead 输出:实际写入的字节数;失败时为 0
     * @return true 写入了一个完整包
     *
     * @detail Buffer 装不下整包时视为协议错误:记 ERROR 日志、置 bConnected = false
     * 并返回 false。内部通过 ReceivePacket 驱动,因此会顺带冲发送缓冲。
     */
    bool Receive(void* Buffer, uint32 Size, uint32& BytesRead) override;

    /**
     * @brief ReceivePacket - 驱动一次"冲发送缓冲 + 收包"循环。
     * @param OutPacket 输出包体;进入时先清空
     * @return true 收到一个完整包;false 暂无完整包 / 对端关闭 / 出错 / 缓冲溢出
     *
     * @detail 循环直到解出包或 socket 报 WouldBlock:先 ProcessRecvBuffer 试解,
     * 不够再一次性读最多 8192 字节追加到 RecvBuffer。
     * 对端正常关闭(recv 返回 0)与连接复位都会置 bConnected = false。
     */
    bool ReceivePacket(TByteArray& OutPacket) override;

    /**
     * @brief FlushSendBuffer - 反复写出 SendBuffer 直到发完或被 WouldBlock 挡住。
     * @return true 缓冲已空,或本次因 WouldBlock 提前返回(可重试);false 发送出错(已置断开)
     *
     * @detail 每次成功发送后都用 erase 从头部摘除已发部分,单次调用是 O(发出字节数)
     * 的搬移开销。
     */
    bool FlushSendBuffer() override;

    /**
     * @brief HasPendingSendData - 是否还有没发完的数据。
     */
    bool HasPendingSendData() const override {
        return !SendBuffer.empty();
    }
    uint64 GetPlayerId() const override {
        return PlayerId;
    }
    void SetPlayerId(uint64 Id) override {
        PlayerId = Id;
    }
    TSocketFd GetSocketFd() const override {
        return Socket.Get();
    }
    void SetNonBlocking(bool bNonBlocking) override;
    bool IsConnected() const override {
        return bConnected;
    }

    /**
     * @brief Close - 关闭连接并清空收发缓冲。
     *
     * @detail 幂等:已断开时只清缓冲。会先 ShutdownSocket 再释放 fd,让对端尽快收到 FIN。
     */
    void Close() override;

    // 获取远端地址
    const MString& GetRemoteAddress() const {
        return RemoteAddress;
    }
    uint16 GetRemotePort() const {
        return RemotePort;
    }

    /**
     * @brief ProcessRecvBuffer - 从 RecvBuffer 里解出一个包。
     * @param OutPacket 输出包体
     * @return true 解出完整包
     *
     * @detail 解包失败(长度前缀非法:0 或 > MAX_PACKET_SIZE)时记 ERROR 日志并置连接断开。
     */
    bool ProcessRecvBuffer(TByteArray& OutPacket) override;
};

/**
 * @brief MSocket - socket 便捷工厂与静态操作入口(全部静态,无状态)。
 *
 * @detail 只是 MSocketPlatform 的语义化包装(补上 bind/listen/accept 等组合操作与日志),
 * 不做任何连接管理:返回的裸 fd 需要调用方自己用 MSocketHandle 或 MSocket::Close 管理。
 */
class MSocket {
    public:
    /**
     * @brief EnsureInit - 平台初始化(Windows: WSAStartup;Unix: 无操作)。
     * @return true 可用
     */
    static bool EnsureInit();

    /**
     * @brief CreateListenSocket - 创建已 bind + listen 的非阻塞监听 socket。
     * @param Port       监听端口(主机字节序),绑定到 INADDR_ANY
     * @param MaxBacklog listen() 的 backlog
     * @return 监听 fd;任一步失败返回 INVALID_SOCKET_FD(中途创建的 fd 已关闭)
     *
     * @detail 已开 SO_REUSEADDR,避免进程重启时被 TIME_WAIT 占住端口。
     */
    static TSocketFd CreateListenSocket(uint16 Port, int32 MaxBacklog = 128);

    /**
     * @brief CreateNonBlockingSocket - 创建非阻塞且开启 TCP_NODELAY 的 TCP socket。
     * @return 新 fd;失败返回 INVALID_SOCKET_FD
     */
    static TSocketFd CreateNonBlockingSocket();

    /**
     * @brief SetNonBlocking - 设置阻塞/非阻塞模式。
     * @return true 设置成功
     */
    static bool SetNonBlocking(TSocketFd Fd, bool bNonBlocking);

    /**
     * @brief SetNoDelay - 设置 TCP_NODELAY(关闭 Nagle)。
     * @return true 设置成功
     */
    static bool SetNoDelay(TSocketFd Fd, bool bNoDelay);

    /**
     * @brief Accept - 接受连接,并同时给出对端地址。
     * @param ListenFd   监听 fd
     * @param OutAddress 输出:对端 IP;失败时为 "unknown"
     * @param OutPort    输出:对端端口;失败时为 0
     * @return 新连接 fd(非阻塞 + TCP_NODELAY 已设置);失败返回 INVALID_SOCKET_FD
     *
     * @detail 返回的是裸 fd(所有权已从 SAcceptedSocket 释放),调用方负责关闭。
     */
    static TSocketFd Accept(TSocketFd ListenFd, MString& OutAddress, uint16& OutPort);

    /**
     * @brief AcceptConnection - 接受连接,返回带 RAII 句柄的结果。
     * @return 成功时 Socket 有效;失败时各项为空(IsValid() == false)
     */
    static SAcceptedSocket AcceptConnection(TSocketFd ListenFd);

    /**
     * @brief Close - 关闭 fd(不做 shutdown;优雅关闭请先 MSocketPlatform::ShutdownSocket)。
     */
    static void Close(TSocketFd Fd);

    /**
     * @brief GetLastError - 上一次 socket 操作的错误码(Windows: WSAGetLastError;Unix: errno)。
     */
    static int GetLastError();

    /**
     * @brief IsWouldBlock - 该错误码是否表示"暂不可收发,稍后重试"。
     */
    static bool IsWouldBlock(int Error);
};
