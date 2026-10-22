/**
 * @file NetServerBase.h
 * @brief NetServerBase - 各服统一的事件循环骨架基类。
 *
 * @detail 结构:
 *   - 持有一个主事件循环组 MEventLoopGroup(MasterLoop),其内注册若干子循环:
 *     MTaskEventLoop(任务续体)与 MNetEventLoop(网络 poll);
 *   - Run() 的骨架:注册 listener → 注册子循环 → while(bRunning){ MasterLoop.RunOnce();
 *     TickBackends(); PumpServerCallMaintenance(); } → 注销 listener;
 *   - 子类必须实现 GetListenPort() / OnAccept() / ShutdownConnections(),
 *     可选覆盖 TickBackends() 与 OnRunStarted()。
 *
 *   异步续体投递:Run() 把 MAsync::MLoopAsyncContext 绑到 GetTaskRunner()(= TaskLoop),
 *   续体经 IExecutor::Post 回到主线程执行(见 NetServerBase.cpp 的 LoopContext)。
 *
 *   多 Reactor 扩展:可选调 InitSubPool(N) 启动 N 个 Sub reactor 接管网络 I/O。
 *   - SubCount == 0(默认):单 Reactor 路径,主线程自己 poll + dispatch —— 行为与未启用时完全一致;
 *   - SubCount > 0:网络 poll 交给 SubPool,OnAccept 按 remote_addr hash 把连接派发到目标 Sub。
 *   设计文档:Docs/superpowers/specs/2026-08-13-multi-reactor-design.md。
 */

#pragma once

#include "Common/IO/Socket/Socket.h" // 拿 INetConnection 完整定义(DispatchConnection 要调 GetRemoteAddress)
#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Async/IExecutor.h"
#include "Common/Runtime/Concurrency/SubReactorPool.h"
#include "Common/Runtime/EventLoop/EventLoopGroup.h"
#include "Common/Runtime/EventLoop/NetEventLoop.h"
#include "Common/Runtime/EventLoop/TaskEventLoop.h"
#include "Common/Runtime/MLib.h"

/**
 * @brief MNetServerBase - 服务器主循环骨架(子类提供端口、accept 处理与收尾清理)。
 *
 * @detail 线程约定:Run() 在主线程阻塞执行;启用 SubPool 后,网络 I/O 在 Sub 线程,
 *   但 OnAccept / TickBackends / 业务回调都仍在主线程,业务侧不需要自己加锁。
 */
class MNetServerBase {
    public:
    virtual ~MNetServerBase() = default;

    /**
     * @brief Run - 阻塞式主循环,Init 成功后由 main 调用。
     *
     * @detail 返回即表示已收到关闭信号(MSignalHandler)或 bRunning 被置否;退出前会注销
     *   listener、Shutdown SubPool、清掉 ambient async context。bRunning 为 false 时
     *   直接返回并打错误日志(即"未初始化就 Run")。
     */
    virtual void Run();

    /// @brief 请求退出主循环:置 bRunning = false,并 Stop 所有网络 loop 唤醒阻塞的 poll。
    void RequestShutdown();

    /// @brief 关闭连接并释放监听:调 ShutdownConnections() 做子类清理,并注销 listener。
    /// @note 幂等(内部由 bShutdownDone 保证只执行一次)。
    void Shutdown();

    /// @return 绑给异步续体用的执行器(= 任务子循环 TaskLoop)。
    ::mession::async::IExecutor* GetExecutor() {
        return &TaskLoop;
    }

    /**
     * @brief InitSubPool - 启动 N 个 Sub reactor 接管网络 I/O(多 Reactor 模式)。
     *
     * @param InSubCount Sub 数量,通常取 std::thread::hardware_concurrency();传 0 表示不启用
     *
     * @detail 必须在 Run() 之前调用,且只调用一次。不启用(默认)时走单 Reactor 路径,
     *   行为完全不变。启用后网络 poll 跑在 Sub 自己的线程上,主线程只负责 listen + accept + 派发。
     */
    void InitSubPool(uint32 InSubCount);

    /**
     * @brief DispatchConnection - 把一条新连接注册进网络 poll 集合。
     * @param InConnId   连接 id
     * @param InConn     连接对象
     * @param InOnRead   收包回调
     * @param InOnClose  断开回调
     *
     * @detail 取代业务直接调 EventLoop.RegisterConnection:
     *   - SubCount == 0:直接注册进本类的 EventLoop;
     *   - SubCount > 0:按 remote_addr hash 选出目标 Sub,Post 到该 Sub 的线程注册。
     *   业务无需感知 Sub 的存在,只调这一个基类接口。
     */
    void DispatchConnection(uint64 InConnId, TSharedPtr<INetConnection> InConn, TFunction<void(uint64, const TByteArray&)> InOnRead, TFunction<void(uint64)> InOnClose);

    /** @return Sub 数量(0 = 单 Reactor 模式). */
    uint32 GetSubCount() const {
        return SubCount;
    }

    /** @return SubPool 指针(未 InitSubPool 时 = nullptr). */
    MSubReactorPool* GetSubPool() {
        return SubPool.get();
    }

    protected:
    MEventLoopGroup MasterLoop;               ///< 主事件循环组:容纳下面的子循环
    MTaskEventLoop  TaskLoop;                 ///< 任务子循环:承载异步续体
    MNetEventLoop   EventLoop;                ///< 网络子循环。单 Reactor 模式下是唯一的网络 loop;多 Reactor 模式下改由 SubPool 持有 N 个独立 loop,本对象仍负责"管理连接"(见 Run())
    uint64          ListenerId       = 0;     ///< 公网 listener 的 id;0 表示未注册
    uint64          InternalListenerId = 0;   ///< 可选内部 RPC listener 的 id;0 表示未启用
    bool            bRunning         = false; ///< false 时 Run() 会直接返回(等于"未初始化")
    bool            bShutdownDone    = false; ///< Shutdown() 的幂等标志
    bool            bStepsRegistered = false; ///< 子循环是否已挂到 MasterLoop(避免重复挂)

    /// 绑到 TaskLoop 的 ambient MAsyncContext;在 Run() 里安装、退出时清空。
    TSharedPtr<MAsync::MLoopAsyncContext> LoopContext;

    // 多 Reactor 扩展:SubCount > 0 时启用
    uint32                      SubCount = 0; ///< Sub reactor 数量;0 = 单 Reactor 模式
    TUniquePtr<MSubReactorPool> SubPool;      ///< Sub reactor 池;未启用时为空

    /// @return 监听端口;由子类从配置返回。
    virtual uint16 GetListenPort() const = 0;

    /// @return 内部 RPC 监听端口；0 表示该服务不分离 internal listener。
    virtual uint16 GetInternalListenPort() const {
        return 0;
    }

    /// 新连接到达时调用。子类在此建立对端/客户端结构并调用 DispatchConnection 完成注册。
    virtual void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) = 0;

    /// 内部 RPC listener 的 accept 回调；默认复用公网处理，兼容单端口服务。
    virtual void OnInternalAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) {
        OnAccept(ConnId, std::move(Conn));
    }

    /** @brief 把 accept 回调投递至该连接所属 Sub，避免 Sub#0 成为业务状态所有者。 */
    void DispatchAcceptedConnection(uint64 ConnId, TSharedPtr<INetConnection> Conn, bool bInternal);

    /// 每帧在 MasterLoop.RunOnce() 之后调用,用于后端 Tick、定时器、会话清理等;默认空实现。
    virtual void TickBackends() {
    }

    /// Shutdown 时调用:关闭所有连接、清空容器、断开 MServerConnection 等;必须由子类实现。
    virtual void ShutdownConnections() = 0;

    /// 主循环开始前调用(此时监听已注册成功),可用于打启动日志;默认空实现。
    virtual void OnRunStarted() {
    }
};
