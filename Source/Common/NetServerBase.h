#pragma once

#include "Core/Net/NetCore.h"
#include "Core/Event/MEventLoop.h"
#include "Core/Event/EventLoop.h"
#include "Core/Event/TaskEventLoop.h"
#include "Core/Concurrency/ITaskRunner.h"

/**
 * 各服统一事件循环模板基类：
 * - 持有一个主事件循环 MEventLoop（MasterLoop），其内注册若干子循环：MTaskEventLoop（任务）、MNetEventLoop（网络 poll）
 * - Run()：RegisterListener → 注册子循环 → while(bRunning){ MasterLoop.RunOnce(); TickBackends(); } → UnregisterListener
 * - 子类实现：GetListenPort()、OnAccept()、ShutdownConnections()，可选覆盖 TickBackends()
 * - 异步（Yield/Sequence）使用 GetTaskRunner() 投递任务
 */
class MNetServerBase
{
public:
    virtual ~MNetServerBase() = default;

    /** 执行主循环（在 Init 成功后由 main 调用） */
    virtual void Run();
    void RequestShutdown();
    /** 关闭连接并释放监听；子类通过 ShutdownConnections() 做具体清理 */
    void Shutdown();

    /** 供异步（MAsync::Yield、MSequence）投递任务用 */
    ITaskRunner* GetTaskRunner() { return &TaskLoop; }

protected:
    MEventLoop MasterLoop;
    MTaskEventLoop TaskLoop;
    MNetEventLoop EventLoop;
    uint64 ListenerId = 0;
    bool bRunning = false;
    bool bShutdownDone = false;
    bool bStepsRegistered = false;

    /** 监听端口，由子类从配置返回 */
    virtual uint16 GetListenPort() const = 0;
    /** 新连接到达时调用；子类在此创建对端/客户端结构并调用 EventLoop.RegisterConnection */
    virtual void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) = 0;
    /** 每帧在 RunOnce 之后调用，用于后端 Tick、定时器、会话清理等；默认空实现 */
    virtual void TickBackends() {}
    /** Shutdown 时关闭所有连接、清空容器、断开 MServerConnection 等 */
    virtual void ShutdownConnections() = 0;
    /** 主循环开始前调用（监听已注册成功），可用于打日志 */
    virtual void OnRunStarted() {}
};
