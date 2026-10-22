/**
 * @file ServerConnection.h
 * @brief ServerConnection - 服务器间连接的抽象、配置与连接管理器。
 *
 * @detail 本文件定义服务器间通信的底座:
 *   - EServerType / EServerMessageType:线上可见的服务类型与包类型枚举;
 *   - SServerInfo / SServerConnectionConfig:服务器与连接配置;
 *   - MMessageChannel(+ MTcpMessageChannel):传输通道抽象,屏蔽 TCP/其它实现;
 *   - MServerConnection:单条服务器间连接,负责握手、心跳、重连与收发;
 *   - MServerConnectionManager:按 ServerId 管理多条连接的容器。
 *
 *   线程模型:本文件的类型都**不是**线程安全的 —— State 与统计字段无锁保护,
 *   约定由所属 EventLoop 的主循环线程调用 Tick / Send。
 */

#pragma once

#include "Common/IO/Socket/Socket.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/MLib.h"
#include <chrono>
#include <thread>

/// 服务器类型。同时是 MEndpointCache 的路由键、RPC 端点映射的输入,以及
/// Protocol/Messages 里消息路由的依据。
/// 新增类型时必须同步:RpcManifest.cpp::GetServerEndpointClassName 的映射表。
enum class EServerType : uint32 {
    Unknown = 0,
    Gateway = 1,
    Login   = 2,
    World   = 3,
    Scene   = 4,
    Router  = 5,
    Mgo     = 6,
    // PoC 阶段的第一个 Service 类型;枚举名是临时的,投产前应改为业务名(如 CombatService=7)。
    Echo = 7,
    // Player 业务进程(per-player actor 容器,协议下沉到 ActorMember)。
    // 8 这个值之前预留给 SampleB,已被 Player 占用。
    Player = 8,
};

/// 包类型首字节。这些值随包上线,属于线上兼容面:不可改值、不可重排。
/// 27-29 是服务器间调用的低段,0xCD/0xCE 是高段(与客户端 envelope 等协议族区分)。
enum class EServerMessageType : uint8 {
    MT_RPC              = 27,   ///< 服务器间 RPC 调用(负载见 RpcTransport::BuildServerRpcPayload)
    MT_FunctionCall     = 28,   ///< 服务器间反射函数调用请求(带 CallId,期待 MT_FunctionResponse)
    MT_FunctionResponse = 29,   ///< 服务器间反射函数调用响应(按 CallId 匹配 RegisterServerCall 注册表)
    MT_ServerPush       = 0xCD, ///< 单向推送 / actor 投递状态 ack:无 CallId 匹配、无回包
    MT_ActorPost        = 0xCE, ///< actor Post 的远端 wire(对应 MActorHandle::Post);无 CallId,
                                ///< 对端收到后直接反射 dispatch 且不回包(消除 wasted RTT)
};

/// 描述一台服务器的身份与地址。注意 Address/ServerName 是 MString(有堆分配),
/// 频繁拷贝的地方请传引用。
struct SServerInfo {
    uint32      ServerId   = 0;
    EServerType ServerType = EServerType::Unknown;
    MString     ServerName;
    MString     Address;
    uint16      Port   = 0;
    uint16      ZoneId = 0;

    SServerInfo() = default;
    SServerInfo(uint32 Id, EServerType Type, const MString& Name, const MString& Addr, uint16 P, uint16 Z = 0) : ServerId(Id), ServerType(Type), ServerName(Name), Address(Addr), Port(P), ZoneId(Z) {
    }
};

/// 构造完整 SServerInfo 的便捷函数(用于 ApplyRoute 等需要一次性填全字段的场景)。
inline SServerInfo MakeServerInfo(uint32 Id, EServerType Type, const MString& Name, const MString& Addr, uint16 Port, uint16 ZoneId = 0) {
    SServerInfo Info;
    Info.ServerId   = Id;
    Info.ServerType = Type;
    Info.ServerName = Name;
    Info.Address    = Addr;
    Info.Port       = Port;
    Info.ZoneId     = ZoneId;
    return Info;
}

/// 连接状态机:Disconnected → Connecting → Connected(TCP 已通) → Authenticated(握手完成)。
/// 注意 IsConnected() 只在 Authenticated 时为真,所以"TCP 通了但还没握手"这段时间里
/// 对外表现为"未连接"。
enum class EConnectionState { Disconnected = 0, Connecting = 1, Connected = 2, Authenticated = 3 };

/// 一条出站服务器连接的配置(对端身份 + 心跳/重连节奏)。
struct SServerConnectionConfig {
    uint32      ServerId   = 0;
    EServerType ServerType = EServerType::Unknown;
    MString     ServerName;
    MString     Address;
    uint16      Port = 0;

    // 心跳/重连配置(秒)
    float HeartbeatInterval = 5.0f; // 心跳间隔
    float ConnectTimeout    = 3.0f; // 连接超时
    float ReconnectInterval = 5.0f; // 重连间隔

    // 下面两个构造只填对端身份字段,心跳/重连间隔保持上面的默认值;
    // MServerConnection 的带参构造内部走 SetConfig,会把这些字段一并搬进连接对象。
    SServerConnectionConfig() = default;

    SServerConnectionConfig(uint32 Id, EServerType Type, const MString& Name, const MString& Addr, uint16 P) : ServerId(Id), ServerType(Type), ServerName(Name), Address(Addr), Port(P) {
    }
};

/// 消息通道抽象:屏蔽具体传输实现(TCP / UDP / WebSocket ...)。
/// 所有实现都应保证方法在"通道已失效"时安全返回 false,而不是崩溃。
class MMessageChannel {
    public:
    virtual ~MMessageChannel()                        = default;
    virtual bool Send(const void* Data, uint32 Size)  = 0;
    virtual bool ReceivePacket(TByteArray& OutPacket) = 0;
    virtual bool IsConnected() const                  = 0;
    virtual void Close()                              = 0;
};

/// 基于 MTcpConnection 的通道实现:纯转发,不做缓冲与重试。
/// 内部 Connection 允许为空,此时所有查询/发送都安全地返回 false(Close 为空操作)。
class MTcpMessageChannel : public MMessageChannel {
    public:
    explicit MTcpMessageChannel(TSharedPtr<MTcpConnection> InConnection) : Connection(std::move(InConnection)) {
    }

    bool Send(const void* Data, uint32 Size) override;
    bool ReceivePacket(TByteArray& OutPacket) override;
    bool IsConnected() const override;
    void Close() override;

    private:
    TSharedPtr<MTcpConnection> Connection;
};

/**
 * @brief MServerConnection - 一条主动出站的服务器间连接。
 *
 * @detail 职责:TCP 建连 → 发握手 RPC 并本地置为 Authenticated → 周期心跳 → 断线自动重连,
 *   并把收到的整包交给 OnMessageCallback。
 *
 *   生命周期:
 *     1. 构造(可带 config) / SetConfig;
 *     2. Connect() —— 立即发起一次建连;失败则等待 Tick 触发重连;
 *     3. 主循环每帧 Tick(DeltaTime) —— 收包、心跳、重连都发生在这里;
 *     4. Disconnect() 显式断开;对象析构只做本地收尾(不会触发 OnDisconnect 回调)。
 *
 *   所有权:继承 TEnableSharedFromThis,必须由 TSharedPtr 持有(内部回调要传自身)。
 *   线程安全:无锁,假定 Tick/Connect/Disconnect 都在同一条事件循环线程上调用;
 *   State 与各统计字段都不做同步。
 */
class MServerConnection : public TEnableSharedFromThis<MServerConnection> {
    private:
    TSharedPtr<MMessageChannel> Transport;                              ///< 传输通道;未连接时为空
    EConnectionState            State = EConnectionState::Disconnected; ///< 连接状态机当前态
    SServerConnectionConfig     Config;                                 ///< 对端身份与配置

    // 本地服务器信息(进程级静态,供握手时上报自己)
    static SServerInfo LocalServerInfo;

    // 心跳
    float  HeartbeatTimer    = 0.0f; ///< 距上次心跳的累计时长
    float  HeartbeatInterval = 5.0f; ///< 心跳间隔;由 SetConfig 从配置搬进来(带参构造内部也走 SetConfig)
    uint32 HeartbeatSeq      = 0;    ///< 心跳序号(每次发送前自增)

    // 重连
    float  ReconnectTimer    = 0.0f; ///< 距上次重连尝试的累计时长
    float  ReconnectInterval = 5.0f; ///< 重连间隔;由 SetConfig 从配置搬进来(带参构造内部也走 SetConfig)
    uint32 ReconnectAttempts = 0;    ///< 累计重连尝试次数(只增不减)

    // 统计
    uint64 BytesSent     = 0; ///< 累计发送字节(含每包 1 字节类型头)
    uint64 BytesReceived = 0; ///< 累计接收字节

    // 回调(全部可为空;为空即跳过)
    TFunction<void(TSharedPtr<MServerConnection>)>                           OnConnectCallback;             ///< 连接建立(TCP 通)后回调
    TFunction<void(TSharedPtr<MServerConnection>)>                           OnDisconnectCallback;          ///< 断开时回调(无幂等保护:重复调用 Disconnect() 会重复触发;析构路径不触发)
    TFunction<void(TSharedPtr<MServerConnection>, uint8, const TByteArray&)> OnMessageCallback;             ///< 收到包回调(已剥掉类型字节)
    TFunction<void(TSharedPtr<MServerConnection>, const SServerInfo&)>       OnServerAuthenticatedCallback; ///< 握手完成后回调

    // 日志前缀
    MString LogPrefix;

    public:
    MServerConnection() {
    }

    /// @brief 用配置构造。等价于默认构造之后立刻 SetConfig:对端身份、心跳与重连间隔都会
    /// 从配置搬进连接对象,因此构造完即可直接使用,不必再补一次 SetConfig()。
    explicit MServerConnection(const SServerConnectionConfig& InConfig) {
        SetConfig(InConfig);
    }

    // NOTE: 析构**不能**调用 Disconnect() —— Disconnect() 会用 shared_from_this() 取自身来
    // 构造 OnDisconnect 回调参数,而析构时(尤其是最后一个 TSharedPtr 释放时)对象已无 shared
    // 属主,shared_from_this() 会抛 std::bad_weak_ptr,在隐式 noexcept 的析构里直接
    // std::terminate;即使侥幸还有属主,回调里访问一个正在析构的对象也不安全。
    // 因此这里只做无回调的本地收尾(与 Disconnect() 的前半段等价,只是不触发回调、不打日志);
    // 需要 OnDisconnect 回调的调用方,应在仍持有 TSharedPtr 时显式调用 Disconnect()
    // (注意它没有幂等保护,重复调用会重复触发回调)。
    ~MServerConnection() {
        if (Transport) {
            Transport->Close();
            Transport.reset();
        }
        State = EConnectionState::Disconnected;
    }

    /// @brief 更新配置与日志前缀,并把 HeartbeatInterval / ReconnectInterval 搬进成员。
    void SetConfig(const SServerConnectionConfig& InConfig) {
        Config            = InConfig;
        HeartbeatInterval = InConfig.HeartbeatInterval;
        ReconnectInterval = InConfig.ReconnectInterval;
        UpdateLogPrefix();
    }
    const SServerConnectionConfig& GetConfig() const {
        return Config;
    }

    // 静态方法：设置本服务器信息
    // NOTE: 操作的是**进程级**静态数据,被本进程所有 MServerConnection 共享,且无锁保护;
    // 只应在启动阶段调用一次。
    static void SetLocalInfo(uint32 Id, EServerType Type, const MString& Name) {
        LocalServerInfo.ServerId   = Id;
        LocalServerInfo.ServerType = Type;
        LocalServerInfo.ServerName = Name;
    }

    static const SServerInfo& GetLocalInfo() {
        return LocalServerInfo;
    }

    /// @name 回调设置(覆盖式赋值;传空 TFunction 即取消回调)
    /// @{
    void SetOnConnect(TFunction<void(TSharedPtr<MServerConnection>)> CB) {
        OnConnectCallback = CB;
    }
    void SetOnDisconnect(TFunction<void(TSharedPtr<MServerConnection>)> CB) {
        OnDisconnectCallback = CB;
    }
    void SetOnMessage(TFunction<void(TSharedPtr<MServerConnection>, uint8, const TByteArray&)> CB) {
        OnMessageCallback = CB;
    }
    void SetOnAuthenticated(TFunction<void(TSharedPtr<MServerConnection>, const SServerInfo&)> CB) {
        OnServerAuthenticatedCallback = CB;
    }
    /// @}

    /**
     * @brief Connect - 发起一次建连并立即握手。
     * @return true 连接已建立(或此前已处于 Connected/Authenticated);false 正在连接中,或本次尝试失败
     *
     * @detail 同步阻塞至多 Config.ConnectTimeout 秒(底层 ConnectTo 的语义)。失败时
     *   状态回到 Disconnected,之后由 Tick 的自动重连接管。
     */
    bool Connect();

    /// @brief 关闭连接、复位心跳计时并触发 OnDisconnect 回调。
    /// @note 无幂等保护:重复调用会重复触发回调。
    void Disconnect();

    /**
     * @brief IsConnected - 是否"可用"(已握手完成)。
     * @return true 仅当 State == Authenticated
     *
     * @detail 语义陷阱:TCP 已连通但尚未握手成功(State == Connected)时返回 false。
     *   想判断"TCP/传输层是否还活着"请用 GetState()。
     */
    bool IsConnected() const {
        return State == EConnectionState::Authenticated;
    }

    /// @brief 底层传输是否仍可用于握手等控制消息；不代表远端已认证。
    bool IsTransportConnected() const {
        return (State == EConnectionState::Connected || State == EConnectionState::Authenticated) && Transport && Transport->IsConnected();
    }

    /// @return true:正在建连(Connecting)或已建连但未握手(Connected)。
    bool IsConnecting() const {
        return State == EConnectionState::Connecting || State == EConnectionState::Connected;
    }

    /// @return 原始状态机取值。
    EConnectionState GetState() const {
        return State;
    }

    // 统计查询
    uint64 GetBytesSent() const {
        return BytesSent;
    }
    uint64 GetBytesReceived() const {
        return BytesReceived;
    }
    uint32 GetReconnectAttempts() const {
        return ReconnectAttempts;
    }

    /**
     * @brief SendPacket - 在负载前补 1 字节包类型后发送。
     * @param PacketType 包类型(EServerMessageType 的取值)
     * @param Data       负载;可为空
     * @param Size       负载字节数
     * @return true 已入队;false 状态不是 Connected/Authenticated,或底层发送失败
     *
     * @detail 空负载不会失败:会正常发出一个只有类型字节的包。
     *   Data/Source 的缓冲区在调用返回后即可释放(内部先拷贝)。
     */
    bool SendPacket(uint8 PacketType, const void* Data, uint32 Size);

    /**
     * @brief SendPacketRaw - 原样发送已拼好的完整包,不再补类型字节。
     * @param Data 完整包(含类型字节);为空直接返回 false
     * @return true 已入队并累加 BytesSent;false 传输层为空 / 未连接 / Data 为空
     */
    bool SendPacketRaw(const TByteArray& Data);

    /**
     * @brief SendServerPush - 单向推送 / actor 投递状态 ack(不需要 CallId,不期待回包)。
     *
     * @detail 当前唯一生产者是 actor Post 的远端路径:MEndpointCache::DispatchActorPostMessage
     *   处理完 MT_ActorPost 后,用它把投递状态回给发起方,发起方据此删除 per-actor outbox
     *   条目 —— 这是替代 SFutureResult/MPromise 链路的真正"fire-and-forget"回执。
     *   接收侧由 MActorSystem 的 ServerPush listener 机制消费(业务可注册监听)。
     *
     *   wire format(本端 → 对端,MT_ServerPush 的负载):
     *     [StatusCode:1B][Reserved:2B][Payload:N]
     *   其中本协议当前约定的 Payload 是 16 字节小端的 [ActorId:8B][SequenceId:8B];
     *   两个 Reserved 字节恒写 0,预留给后续演化。
     *   StatusCode:0=Delivered, 1=ActorNotFound, 2=ParseError, 3=QueueFull。
     *
     * @param StatusCode 业务级状态码(见上)
     * @param Payload    可选负载;按右值传入,内部会搬移,调用方传完即失效
     * @return true 已入队到发送缓冲;false 连接未就绪 / 发送缓冲满
     */
    bool SendServerPush(uint8 StatusCode, TByteArray&& Payload = TByteArray());

    /**
     * @brief Tick - 每帧驱动:收包 → 心跳;处于 Disconnected 时按间隔自动重连。
     * @param DeltaTime 距上一帧的秒数
     *
     * @detail 重连与心跳各自用自己的累计计时器;重连失败不会抛出,只累加
     *   ReconnectAttempts 并等下一轮。未配置 Address/Port 的连接在 Disconnected 下
     *   会直接跳过,不做无意义重试。
     */
    void Tick(float DeltaTime);

    // 获取服务器信息
    // WARNING: 返回的是**函数内 static 对象**的引用,全进程所有实例、所有线程共享同一份。
    //   每次调用都会就地覆写它,所以调用方拿到引用后一旦再次调用本函数(或并发调用),
    //   之前引用的内容就会变。需要稳定快照请自行拷贝一份。
    const SServerInfo& GetRemoteServerInfo() const {
        static SServerInfo Info;
        Info.ServerId   = Config.ServerId;
        Info.ServerType = Config.ServerType;
        Info.ServerName = Config.ServerName;
        Info.Address    = Config.Address;
        Info.Port       = Config.Port;
        return Info;
    }

    private:
    /// 按 Config.ServerName 重算日志前缀 "[Server:<name>]"。
    void UpdateLogPrefix();

    /// 真正执行建连:TcpConnection + 包装成 MTcpMessageChannel;失败时状态回 Disconnected。
    bool TryConnect();

    /// 从传输层循环取整包:剥掉类型字节后交给 HandlePacket;检测到对端关闭则 Disconnect。
    void ProcessRecv();

    /// 按包类型分发。当前实现只有 default 分支:所有包(含心跳)都转发给 OnMessageCallback。
    void HandlePacket(uint8 PacketType, const TByteArray& Data);

    /// 发 ServerCall 握手；仅在收到成功响应后置为 Authenticated。
    void SendHandshake();

    /// 发一次心跳 RPC(序号自增)。
    void SendHeartbeat();
};

/// MServerConnectionManager::GetStats 的聚合结果。
struct SConnectionManagerStats {
    size_t Total             = 0;
    size_t Active            = 0;
    uint64 BytesSent         = 0;
    uint64 BytesReceived     = 0;
    uint32 ReconnectAttempts = 0;
};

// 服务器连接管理器
// 说明:当前 Gateway/World/Scene 已部分迁移到"显式持有后端连接"的写法,
// 本管理器按 ServerId 统一持有连接并集中 Tick,是尚未迁移部分的收敛点。
class MServerConnectionManager {
    private:
    // 所有服务器连接,键 = SServerConnectionConfig::ServerId
    TMap<uint32, TSharedPtr<MServerConnection>> Connections;

    // 轮询间隔:Tick 不是每帧都驱动连接,而是按此固定间隔批量驱动。
    float PollInterval = 0.1f;
    float PollTimer    = 0.0f;

    public:
    MServerConnectionManager() {
    }

    /**
     * @brief AddServer - 创建并登记一条到远程服务器的连接(不在此建连)。
     * @param Config 连接配置
     * @return 新建的连接对象;同一 ServerId 已存在时**直接覆盖**旧连接(旧对象不会
     *   被显式 Disconnect,只会在引用计数归零时析构)
     */
    TSharedPtr<MServerConnection> AddServer(const SServerConnectionConfig& Config) {
        auto Conn                    = MakeShared<MServerConnection>(Config);
        Connections[Config.ServerId] = Conn;

        LOG_INFO("[ServerMgr] Added server: %s (%s:%d)", Config.ServerName.c_str(), Config.Address.c_str(), Config.Port);

        return Conn;
    }

    /// @brief AddServer - 便捷重载:用字段直接构造配置。心跳/重连间隔取默认值。
    TSharedPtr<MServerConnection> AddServer(uint32 ServerId, EServerType Type, const MString& Name, const MString& Addr, uint16 Port) {
        SServerConnectionConfig Config(ServerId, Type, Name, Addr, Port);
        return AddServer(Config);
    }

    /// @brief RemoveServer - 断开并移除指定 ServerId 的连接;不存在时为空操作。
    void RemoveServer(uint32 ServerId) {
        auto It = Connections.find(ServerId);
        if (It != Connections.end()) {
            It->second->Disconnect();
            Connections.erase(It);
            LOG_INFO("[ServerMgr] Removed server: %d", ServerId);
        }
    }

    /// @return 指定 ServerId 的连接;不存在返回空指针。
    TSharedPtr<MServerConnection> GetConnection(uint32 ServerId) {
        auto It = Connections.find(ServerId);
        return (It != Connections.end()) ? It->second : nullptr;
    }

    /// @brief SendPacketToServer - 向指定 ServerId 发一个包;连接不存在或未握手完成时返回 false。
    bool SendPacketToServer(uint32 ServerId, uint8 PacketType, const void* Data, uint32 Size) {
        auto Conn = GetConnection(ServerId);
        if (Conn && Conn->IsConnected()) {
            return Conn->SendPacket(PacketType, Data, Size);
        }
        return false;
    }

    /// @brief BroadcastPacket - 向所有"已握手完成"的连接各发一份(发送结果不返回,失败静默丢弃)。
    void BroadcastPacket(uint8 PacketType, const void* Data, uint32 Size) {
        for (auto& [Id, Conn] : Connections) {
            if (Conn->IsConnected()) {
                Conn->SendPacket(PacketType, Data, Size);
            }
        }
    }

    /// @brief Tick - 累加计时,达到 PollInterval 时用**固定的 PollInterval**(而非本帧
    /// 实际 DeltaTime)批量驱动所有连接,保证心跳/重连节奏不受帧率抖动影响。
    void Tick(float DeltaTime) {
        PollTimer += DeltaTime;
        if (PollTimer >= PollInterval) {
            PollTimer = 0.0f;

            for (auto& [Id, Conn] : Connections) {
                Conn->Tick(PollInterval);
            }
        }
    }

    /// @brief ConnectAll - 对尚未 IsConnected() 的连接发起一次 Connect。
    void ConnectAll() {
        for (auto& [Id, Conn] : Connections) {
            if (!Conn->IsConnected()) {
                Conn->Connect();
            }
        }
    }

    /// @brief DisconnectAll - 断开全部连接(连接对象仍留在管理器里,会随 Tick 自动重连)。
    void DisconnectAll() {
        for (auto& [Id, Conn] : Connections) {
            Conn->Disconnect();
        }
    }

    // 获取统计
    /// @return 登记在册的连接总数(含未连接/已断开的)。
    size_t GetTotalCount() const {
        return Connections.size();
    }
    /// @return 已握手完成(IsConnected() 为真)的连接数。
    size_t GetActiveCount() const {
        size_t Count = 0;
        for (auto& [Id, Conn] : Connections) {
            if (Conn->IsConnected()) {
                Count++;
            }
        }
        return Count;
    }

    /// @brief GetStats - 汇总连接数、活跃数、累计收发字节与重连次数。
    SConnectionManagerStats GetStats() const {
        SConnectionManagerStats Stats;
        Stats.Total = Connections.size();
        for (const auto& [Id, Conn] : Connections) {
            (void)Id;
            if (!Conn) {
                continue;
            }
            if (Conn->IsConnected()) {
                ++Stats.Active;
            }
            Stats.BytesSent += Conn->GetBytesSent();
            Stats.BytesReceived += Conn->GetBytesReceived();
            Stats.ReconnectAttempts += Conn->GetReconnectAttempts();
        }
        return Stats;
    }
};
