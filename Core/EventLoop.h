#pragma once

#include "NetCore.h"
#include "Socket.h"
#include "Poll.h"

// 单线程事件循环：监听 + 连接统一 poll，可读时 accept 或收包并回调；支持 PostTask 延后执行。
class MNetEventLoop
{
public:
    using TAcceptCallback = TFunction<void(uint64 ConnectionId, TSharedPtr<INetConnection> Connection)>;
    using TReadCallback = TFunction<void(uint64 ConnectionId, const TArray& Payload)>;
    using TCloseCallback = TFunction<void(uint64 ConnectionId)>;
    using TTask = TFunction<void()>;

    // 注册监听端口；返回 ListenerId，失败返回 0。OnAccept 收到新连接后由调用方再 RegisterConnection。
    uint64 RegisterListener(uint16 Port, TAcceptCallback OnAccept);

    void UnregisterListener(uint64 ListenerId);

    void RegisterConnection(uint64 ConnectionId, TSharedPtr<INetConnection> Connection,
                           TReadCallback OnRead, TCloseCallback OnClose);

    void UnregisterConnection(uint64 ConnectionId);

    /** 将任务投递到下一轮 RunOnce 执行；线程安全。 */
    void PostTask(TTask Task);

    // 执行一次 poll + 分发，TimeoutMs 为 poll 超时。返回处理到的事件数，<0 表示内部错误。
    int32 RunOnce(int TimeoutMs = 100);

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

    TDeque<TTask> PendingTasks;
    mutable std::mutex TaskMutex;
};
