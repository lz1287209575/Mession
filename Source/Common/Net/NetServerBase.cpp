/**
 * @file NetServerBase.cpp
 * @brief MNetServerBase 的实现:listener 注册、单/多 Reactor 主循环、优雅退出。
 */

#include "Common/Net/NetServerBase.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Runtime/Concurrency/SignalHandler.h"
#include "Common/Runtime/Log/Log.h"

void MNetServerBase::InitSubPool(uint32 InSubCount) {
    if (InSubCount == 0) {
        // N=0 = 不启用,走单 Reactor 路径(默认行为)
        SubCount = 0;
        SubPool.reset();
        return;
    }
    SubCount = InSubCount;
    SubPool  = MakeUnique<MSubReactorPool>();
    SubPool->Init(InSubCount);
    LOG_INFO("MNetServerBase: SubPool enabled with %u subs", static_cast<unsigned>(InSubCount));
    // 注意:这里只 Init,Sub 线程要等 Run() 把 listener 注册到 Sub #0 之后才 Start()。
}

void MNetServerBase::DispatchConnection(uint64 InConnId, TSharedPtr<INetConnection> InConn, TFunction<void(uint64, const TByteArray&)> InOnRead, TFunction<void(uint64)> InOnClose) {
    if (SubPool != nullptr && SubCount > 0) {
        // 多 Reactor 模式:按 remote_addr hash 选 Sub,Post 到该 Sub 注册
        const MString RemoteAddr = InConn->GetRemoteAddress();
        const uint32  SubId      = SubPool->PickSub(RemoteAddr);
        SubPool->Post(SubId, [this, SubId, InConnId, InConn, InOnRead = std::move(InOnRead), InOnClose = std::move(InOnClose)]() mutable {
            MNetEventLoop* Loop = SubPool->GetLoop(SubId);
            if (Loop == nullptr) {
                LOG_ERROR("MNetServerBase::DispatchConnection: null Loop for SubId=%u", static_cast<unsigned>(SubId));
                return;
            }
            Loop->RegisterConnection(InConnId, InConn, InOnRead, InOnClose);
        });
        return;
    }

    // 单 Reactor 模式(默认):直接注册到基类 EventLoop,没有跨线程 Post 的开销。
    EventLoop.RegisterConnection(InConnId, InConn, InOnRead, InOnClose);
}

void MNetServerBase::Run() {
    if (!bRunning) {
        LOG_ERROR("Server not initialized (bRunning false)");
        return;
    }

    const uint16 Port = GetListenPort();

    if (SubPool != nullptr && SubCount > 0) {
        // 多 Reactor 模式:在 Sub #0 上注册 listener(由 Sub #0 的 EventLoop 跑 accept + poll)。
        // 当前实现让 Sub #0 同时承担 listen 与它自己那批连接;若以后要把 accept 与 poll 拆开,
        // 可按 multi-reactor 设计引入 MAcceptorReactor
        // (Docs/superpowers/specs/2026-08-13-multi-reactor-design.md)。
        MNetEventLoop* Sub0 = SubPool->GetLoop(0);
        if (Sub0 == nullptr) {
            LOG_ERROR("MNetServerBase: SubPool Sub #0 is null");
            return;
        }
        ListenerId = Sub0->RegisterListener(Port, [this](uint64 ConnId, TSharedPtr<INetConnection> Conn) { OnAccept(ConnId, Conn); });
        if (ListenerId == 0) {
            LOG_ERROR("Failed to register listener on Sub #0, port %d", Port);
            return;
        }
        // 必须等 listener 注册完成后才启动 Sub 线程,否则会跨线程访问 Loop 内部容器
        // (Listeners / Connections),造成数据竞争(典型症状:free(): invalid pointer)。
        SubPool->Start();
    } else {
        // 单 Reactor 模式(默认)
        ListenerId = EventLoop.RegisterListener(Port, [this](uint64 ConnId, TSharedPtr<INetConnection> Conn) { OnAccept(ConnId, Conn); });
        if (ListenerId == 0) {
            LOG_ERROR("Failed to register listener on port %d", Port);
            return;
        }
    }

    OnRunStarted();

    if (!bStepsRegistered) {
        MasterLoop.AddStep(&TaskLoop, 0);
        // 基类 EventLoop 无论哪种模式都要 AddStep:单 Reactor 模式下它是唯一的网络 loop;
        // 多 Reactor 模式下它承载"管理连接"(Registry 连接、MEndpointCache 的出站连接等),
        // 而业务 listener 在 Sub #0、业务连接被分桶到各 Sub。
        // Registry 连接必须靠它被 poll 才能收到包,否则收不到 EndpointChange,
        // FindActor 会 miss 并报 actor_route_invalid。
        MasterLoop.AddStep(&EventLoop, 16);
        bStepsRegistered = true;
    }

    // 安装 ambient MAsyncContext 并绑到本进程的 TaskLoop,使续体能投递回主线程。
    LoopContext = MakeShared<MAsync::MLoopAsyncContext>(GetExecutor());
    MAsync::MAsyncContext::SetCurrent(LoopContext.Get());

    while (bRunning) {
        if (MSignalHandler::IsShutdownRequested()) {
            LOG_INFO("MNetServerBase: shutdown signal received, exiting Run loop");
            RequestShutdown();
            break;
        }
        MasterLoop.RunOnce();
        TickBackends();
        // 每帧清理超时/失联的 ServerCall(在锁外回调,见 PumpServerCallMaintenance)
        PumpServerCallMaintenance();
    }

    // 退出前必须清掉 ambient context,否则 Shutdown 之后会残留悬空指针。
    MAsync::MAsyncContext::SetCurrent(nullptr);
    // 未完成的 future 可能还持有 LoopContext；先断开其非拥有的 TaskLoop 指针，
    // 使这些晚到续体安全丢弃，再释放 context。
    LoopContext->Deactivate();
    LoopContext.reset();

    if (SubPool != nullptr && SubCount > 0) {
        MNetEventLoop* Sub0 = SubPool->GetLoop(0);
        if (Sub0 != nullptr) {
            Sub0->UnregisterListener(ListenerId);
        }
    } else {
        EventLoop.UnregisterListener(ListenerId);
    }
    ListenerId = 0;

    if (SubPool != nullptr) {
        SubPool->Shutdown();
        SubPool.reset();
    }
}

void MNetServerBase::RequestShutdown() {
    bRunning = false;
    if (SubPool != nullptr && SubCount > 0) {
        // Stop 全部 Sub,唤醒它们阻塞中的 poll
        for (uint32 Index = 0; Index < SubCount; ++Index) {
            MNetEventLoop* Loop = SubPool->GetLoop(Index);
            if (Loop != nullptr) {
                Loop->Stop();
            }
        }
    } else {
        EventLoop.Stop();
    }
}

void MNetServerBase::Shutdown() {
    if (bShutdownDone) {
        return;
    }
    bShutdownDone = true;
    bRunning      = false;
    ShutdownConnections();
    if (ListenerId != 0) {
        if (SubPool != nullptr && SubCount > 0) {
            MNetEventLoop* Sub0 = SubPool->GetLoop(0);
            if (Sub0 != nullptr) {
                Sub0->UnregisterListener(ListenerId);
            }
        } else {
            EventLoop.UnregisterListener(ListenerId);
        }
        ListenerId = 0;
    }
}
