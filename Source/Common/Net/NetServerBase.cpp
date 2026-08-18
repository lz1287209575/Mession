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

    // 单 Reactor 模式(默认):基类 EventLoop 直接注册
    EventLoop.RegisterConnection(InConnId, InConn, InOnRead, InOnClose);
}

void MNetServerBase::Run() {
    if (!bRunning) {
        LOG_ERROR("Server not initialized (bRunning false)");
        return;
    }

    const uint16 Port = GetListenPort();

    if (SubPool != nullptr && SubCount > 0) {
        // 多 Reactor 模式:在 Sub #0 上注册 listener(Sub #0 的 EventLoop 跑 accept + poll)
        // 当前简单实现:让 Sub #0 同时管 listen 和该 Sub 上的 conn;
        // 后续若要 split acceptor / sub,可引入 MAcceptorReactor(见 multi-reactor spec §6.3)。
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
        // listener 注册完成后才启动 Sub 线程 —— 避免跨线程访问 Loop 内部
        // 容器(Listeners / Connections)的数据竞争(free(): invalid pointer)。
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
        // 基类 EventLoop 始终 AddStep —— 单 Reactor 模式它是唯一网络 loop;
        // 多 Reactor 模式下它承载 Registry 连接 / MEndpointCache 出站连接等
        // "管理连接"(业务 listener 在 Sub #0,业务连接分桶到各 Sub)。
        // Registry 连接的收包依赖它被 poll,否则 EndpointChange 收不到,
        // FindActor miss(actor_route_invalid)。
        MasterLoop.AddStep(&EventLoop, 16);
        bStepsRegistered = true;
    }

    // P1: install ambient MAsyncContext bound to this process's TaskLoop.
    LoopContext = MakeShared<MAsync::MLoopAsyncContext>(GetTaskRunner());
    MAsync::MAsyncContext::SetCurrent(LoopContext.Get());

    while (bRunning) {
        if (MSignalHandler::IsShutdownRequested()) {
            LOG_INFO("MNetServerBase: shutdown signal received, exiting Run loop");
            RequestShutdown();
            break;
        }
        MasterLoop.RunOnce();
        TickBackends();
        PumpServerCallMaintenance();
    }

    // P1: clear ambient so no dangling pointer survives Shutdown.
    MAsync::MAsyncContext::SetCurrent(nullptr);
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
        // Stop 所有 Sub
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
