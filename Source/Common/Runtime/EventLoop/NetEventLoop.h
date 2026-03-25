#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/EventLoop/EventLoopStep.h"
#include "Common/IO/Socket/Socket.h"

/** 单线程网络事件循环：仅负责监听 + 连接 poll，可读时 accept 或收包并回调；不包含任务队列。实现 IEventLoopStep。 */
class MNetEventLoop : public IEventLoopStep
{
public:
    using TAcceptCallback = TFunction<void(uint64 ConnectionId, TSharedPtr<INetConnection> Connection)>;
    using TReadCallback = TFunction<void(uint64 ConnectionId, const TByteArray& Payload)>;
    using TCloseCallback = TFunction<void(uint64 ConnectionId)>;

    // 注册监听端口；返回 ListenerId，失败返回 0。OnAccept 收到新连接后由调用方再 RegisterConnection。
    uint64 RegisterListener(uint16 Port, TAcceptCallback OnAccept);

    void UnregisterListener(uint64 ListenerId);

    void RegisterConnection(uint64 ConnectionId, TSharedPtr<INetConnection> Connection,
                           TReadCallback OnRead, TCloseCallback OnClose);

    void UnregisterConnection(uint64 ConnectionId);

    /** 执行一次 poll + 分发；TimeoutMs 为 poll 超时。返回处理到的事件数，<0 表示内部错误。 */
    void RunOnce(int TimeoutMs = 100) override;

    // 循环 RunOnce 直到 Stop() 被调用（可在回调中调用）。
    void Run();
    void Stop() { bRunning = false; }

    bool IsRunning() const { return bRunning; }

private:
    struct SListener
    {
        uint64 Id = 0;
        MSocketHandle Handle;
        uint16 Port = 0;
        TAcceptCallback OnAccept;
    };
    struct SConnection
    {
        uint64 Id = 0;
        TSharedPtr<INetConnection> Conn;
        TReadCallback OnRead;
        TCloseCallback OnClose;
    };

    TVector<SListener> Listeners;
    TMap<uint64, SConnection> Connections;
    uint64 NextListenerId = 1;
    bool bRunning = false;
};
