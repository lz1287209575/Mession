/**
 * @file NetEventLoop.h
 * @brief MNetEventLoop —— 单线程 reactor:负责监听与连接 poll,就绪时分发回调。
 *
 * @detail 本类只做网络 I/O、不含任务队列,因此不实现 IExecutor(职责划分见
 *   Async/IExecutor.h:网络循环负责 poll fd,异步续体投递到配对的
 *   MTaskEventLoop)。监听 socket 由 MSocketHandle RAII 持有,
 *   UnregisterListener 直接析构句柄即关闭 fd。
 */
#pragma once

#include "Common/IO/Socket/Socket.h"
#include "Common/Runtime/EventLoop/IEventLoop.h"
#include "Common/Runtime/MLib.h"

/**
 * @brief MNetEventLoop - 单线程网络事件循环(监听 + 连接 poll)。
 *
 * @detail 线程模型与生命期:
 *   1. RegisterListener(Port, OnAccept) —— 建立监听,得到 ListenerId
 *   2. OnAccept 回调里拿到新连接,调用方需再 RegisterConnection() 才会收包
 *   3. RunOnce()/Run() 必须始终在同一线程调用,回调也在该线程执行
 *      (回调内可安全注册/注销,但不可重入 RunOnce)
 *   4. Unregister 系列只从容器移除:监听句柄析构时关闭 fd,连接则等
 *      TSharedPtr 全部释放时关闭,均不会回调 OnClose
 */
class MNetEventLoop : public IEventLoop {
    public:
    using TAcceptCallback = TFunction<void(uint64 ConnectionId, TSharedPtr<INetConnection> Connection)>;
    /** @brief 收包回调:每切出一个完整包调用一次(切包由 INetConnection::ReceivePacket 负责)。 */
    using TReadCallback = TFunction<void(uint64 ConnectionId, const TByteArray& Payload)>;
    /** @brief 关闭回调:POLLERR/POLLHUP,或收包后发现连接已断时触发。 */
    using TCloseCallback = TFunction<void(uint64 ConnectionId)>;

    /**
     * @brief RegisterListener - 创建监听 socket 并注册 Accept 回调。
     * @param Port 监听端口
     * @param OnAccept 新连接回调,可为空;回调内需自行 RegisterConnection 才会收包
     * @return ListenerId(从 1 递增);平台 socket 初始化失败或建监听失败时返回 0
     *
     * @detail 首次调用会隐式初始化平台 socket 库;注册后要到下一次 RunOnce()
     *   才真正进入 poll。backlog 固定 128。
     */
    uint64 RegisterListener(uint16 Port, TAcceptCallback OnAccept);

    /**
     * @brief UnregisterListener - 移除监听并关闭其监听 fd。
     * @param ListenerId RegisterListener 返回的 id;不存在时静默无操作
     */
    void UnregisterListener(uint64 ListenerId);

    /**
     * @brief RegisterConnection - 把已建立的连接纳入 poll 集合。
     * @param ConnectionId 调用方分配的连接 id(accept 路径通常用 MUniqueIdGenerator::Generate())
     * @param Connection 连接对象,本类持一份 TSharedPtr;为空或 IsConnected()==false 时直接忽略
     * @param OnRead 收包回调,可空
     * @param OnClose 关闭回调,可空
     *
     * @detail 同一 ConnectionId 重复注册会覆盖旧条目,旧连接随之释放其 TSharedPtr。
     */
    void RegisterConnection(uint64 ConnectionId, TSharedPtr<INetConnection> Connection, TReadCallback OnRead, TCloseCallback OnClose);

    /**
     * @brief UnregisterConnection - 移除连接条目。
     * @param ConnectionId 连接 id;不存在时静默无操作
     *
     * @detail 只释放本类持有的 TSharedPtr,不调用 OnClose;连接若无其他持有者,
     *   析构时关闭 socket。
     */
    void UnregisterConnection(uint64 ConnectionId);

    /**
     * @brief RunOnce - 执行一次 poll,并把就绪事件分发到回调。
     * @param TimeoutMs poll 超时(毫秒),默认 100
     *
     * @detail 没有可 poll 的 fd 时(如多 Reactor 的 Sub 线程尚未派发连接)不会立即
     *   返回,而是按 TimeoutMs 阻塞等待,否则外层 while 循环会空转吃满 CPU。
     *   需要关闭的连接先收集到 ToClose,遍历完 poll 结果后统一 erase,
     *   避免边遍历边改 Connections。
     */
    void RunOnce(int TimeoutMs = 100) override;

    /** @brief Run - 以 100ms 超时循环调用 RunOnce(),直到 Stop() 被调用。 */
    void Run();
    /** @brief Stop - 清除运行标志;可在回调内调用以请求退出 Run()。 */
    void Stop() {
        bRunning = false;
    }

    /** @brief IsRunning - 当前是否处于 Run() 循环中。 */
    bool IsRunning() const {
        return bRunning;
    }

    private:
    struct SListener {
        uint64          Id = 0;
        MSocketHandle   Handle;
        uint16          Port = 0;
        TAcceptCallback OnAccept;
    };
    struct SConnection {
        uint64                     Id = 0;
        TSharedPtr<INetConnection> Conn;
        TReadCallback              OnRead;
        TCloseCallback             OnClose;
    };

    TVector<SListener>        Listeners;
    TMap<uint64, SConnection> Connections;
    uint64                    NextListenerId = 1; // 从 1 起编号,0 保留给"注册失败"
    bool                      bRunning       = false;
};
