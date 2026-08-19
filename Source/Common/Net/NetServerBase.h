#pragma once

#include "Common/IO/Socket/Socket.h" // 拿 INetConnection 完整定义(DispatchConnection 要调 GetRemoteAddress)
#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Concurrency/ITaskRunner.h"
#include "Common/Runtime/Concurrency/SubReactorPool.h"
#include "Common/Runtime/EventLoop/EventLoopGroup.h"
#include "Common/Runtime/EventLoop/NetEventLoop.h"
#include "Common/Runtime/EventLoop/TaskEventLoop.h"
#include "Common/Runtime/MLib.h"

/**
 * 各服统一事件循环模板基类：
 * - 持有一个主事件循环 MEventLoopGroup（MasterLoop），其内注册若干子循环：MTaskEventLoop（任务）、MNetEventLoop（网络 poll）
 * - Run()：RegisterListener → 注册子循环 → while(bRunning){ MasterLoop.RunOnce(); TickBackends(); } → UnregisterListener
 * - 子类实现：GetListenPort()、OnAccept()、ShutdownConnections()，可选覆盖 TickBackends()
 * - 异步续体投递:Run() 内把 MAsync::MLoopAsyncContext 绑定到 GetTaskRunner()(= TaskLoop),
 *   续体经 IExecutor::Post 投递回主线程执行(见 NetServerBase.cpp 的 LoopContext)
 *
 * 多 Reactor 扩展(P5):可选 InitSubPool(N) 启动 N 个 Sub reactor 接管网络 I/O。
 * - SubCount == 0(默认):走单 Reactor 路径,主线程自己 poll + dispatch(完全等价于 P4 行为)
 * - SubCount > 0:Run 主循环把 poll 交给 SubPool,OnAccept 派发到目标 Sub(按 remote_addr hash)
 */
class MNetServerBase {
    public:
    virtual ~MNetServerBase() = default;

    /** 执行主循环（在 Init 成功后由 main 调用） */
    virtual void Run();
    void         RequestShutdown();
    /** 关闭连接并释放监听；子类通过 ShutdownConnections() 做具体清理 */
    void Shutdown();

    /** 供异步续体投递任务用（MLoopAsyncContext 绑定到此执行器） */
    ITaskRunner* GetTaskRunner() {
        return &TaskLoop;
    }

    /**
     * @brief InitSubPool - 启动 N 个 Sub reactor 接管网络 I/O(多 Reactor 模式).
     *
     * 必须在 Run() 之前调用一次,默认 N=0 = 不启用(走单 Reactor 路径,完全不变)。
     * 启用后,网络 poll 在 Sub 自己的线程跑,主线程只做 listen + accept + 派发。
     *
     * @param InSubCount  Sub 数量,通常 = std::thread::hardware_concurrency()
     */
    void InitSubPool(uint32 InSubCount);

    /**
     * @brief DispatchConnection - 业务 OnAccept 调本接口把 conn 注册到目标 Sub.
     *
     * 取代业务直接调 EventLoop.RegisterConnection 的写法。
     * - SubCount == 0:直接在本类 EventLoop 注册(等同既有行为)
     * - SubCount > 0:Post 到 SubPool->PickSub(remote_addr) 选的目标 Sub,Sub 自己 RegisterConnection
     *
     * 业务不感知 Sub 存在,只调一次基类接口。
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
    MEventLoopGroup MasterLoop;
    MTaskEventLoop  TaskLoop;
    MNetEventLoop   EventLoop; // 单 Reactor 模式用;多 Reactor 模式由 SubPool 持有 N 个独立的
    uint64          ListenerId       = 0;
    bool            bRunning         = false;
    bool            bShutdownDone    = false;
    bool            bStepsRegistered = false;

    /** P1: ambient MAsyncContext bound to TaskLoop; installed in Run(), cleared at exit. */
    TSharedPtr<MAsync::MLoopAsyncContext> LoopContext;

    // P5: 多 Reactor 扩展 — N>0 时启用
    uint32                      SubCount = 0;
    TUniquePtr<MSubReactorPool> SubPool;

    /** 监听端口，由子类从配置返回 */
    virtual uint16 GetListenPort() const = 0;
    /** 新连接到达时调用；子类在此创建对端/客户端结构并调用 DispatchConnection */
    virtual void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) = 0;
    /** 每帧在 RunOnce 之后调用，用于后端 Tick、定时器、会话清理等；默认空实现 */
    virtual void TickBackends() {
    }
    /** Shutdown 时关闭所有连接、清空容器、断开 MServerConnection 等 */
    virtual void ShutdownConnections() = 0;
    /** 主循环开始前调用（监听已注册成功），可用于打日志 */
    virtual void OnRunStarted() {
    }
};
