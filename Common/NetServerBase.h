#pragma once

#include "Core/NetCore.h"
#include "Core/EventLoop.h"

/**
 * 各服统一事件循环模板基类：
 * - 持有 MNetEventLoop、ListenerId、bRunning、bShutdownDone
 * - Run()：RegisterListener(GetListenPort()) → while(bRunning){ RunOnce(16); TickBackends(); } → UnregisterListener
 * - 子类实现：GetListenPort()、OnAccept()、ShutdownConnections()，可选覆盖 TickBackends()
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

protected:
    MNetEventLoop EventLoop;
    uint64 ListenerId = 0;
    bool bRunning = false;
    bool bShutdownDone = false;

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
