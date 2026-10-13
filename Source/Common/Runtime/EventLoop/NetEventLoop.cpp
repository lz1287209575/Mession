/**
 * @file NetEventLoop.cpp
 * @brief MNetEventLoop 实现:注册监听/连接 + 一次 poll 的事件分发。
 *
 * @detail 全部状态(监听表、连接表)只允许在 RunOnce/Run 所在线程访问;
 *   分发期间的容器变更被刻意推迟到遍历结束(见 RunOnce 的 ToClose)。
 */
#include "Common/Runtime/EventLoop/NetEventLoop.h"
#include "Common/IO/Socket/Poll.h"
#include "Common/Runtime/Id.h"

#include <algorithm>

/**
 * @brief 创建监听 socket 并登记 Accept 回调。
 * @param Port 监听端口
 * @param OnAccept 新连接回调;为空时也会接受连接,只是无人处理
 * @return 新的 ListenerId;任何一步失败(初始化/socket 创建)返回 0
 *
 * @detail 句柄所有权立即交给 SListener::Handle(RAII),此后监听 fd 的关闭
 *   只由 UnregisterListener 或本对象析构触发。
 */
uint64 MNetEventLoop::RegisterListener(uint16 Port, TAcceptCallback OnAccept) {
    if (!MSocket::EnsureInit()) {
        return 0;
    }
    TSocketFd Fd = MSocket::CreateListenSocket(Port, 128);
    if (Fd == INVALID_SOCKET_FD) {
        return 0;
    }
    const uint64 Id = NextListenerId++;
    SListener    L;
    L.Id = Id;
    L.Handle.Reset(Fd);
    L.Port     = Port;
    L.OnAccept = std::move(OnAccept);
    Listeners.push_back(std::move(L));
    return Id;
}

/**
 * @brief 从监听表里删除指定条目(析构 Handle 即关闭 fd)。
 * @param ListenerId 目标 id;不存在时什么也不做
 */
void MNetEventLoop::UnregisterListener(uint64 ListenerId) {
    auto It = std::remove_if(Listeners.begin(), Listeners.end(), [ListenerId](const SListener& L) { return L.Id == ListenerId; });
    Listeners.erase(It, Listeners.end());
}

/**
 * @brief 把一条已建立的连接加入 poll 集合。
 * @param ConnectionId 连接 id,作为回调参数原样回传
 * @param Connection 连接对象,接管其 TSharedPtr;为空或未连接时直接丢弃
 * @param OnRead 收包回调
 * @param OnClose 关闭回调
 */
void MNetEventLoop::RegisterConnection(uint64 ConnectionId, TSharedPtr<INetConnection> Connection, TReadCallback OnRead, TCloseCallback OnClose) {
    if (!Connection || !Connection->IsConnected()) {
        return;
    }
    SConnection C;
    C.Id                      = ConnectionId;
    C.Conn                    = std::move(Connection);
    C.OnRead                  = std::move(OnRead);
    C.OnClose                 = std::move(OnClose);
    Connections[ConnectionId] = std::move(C);
}

/**
 * @brief 从连接表删除条目(不触发 OnClose,仅释放本类持有的引用)。
 * @param ConnectionId 目标 id;不存在时什么也不做
 */
void MNetEventLoop::UnregisterConnection(uint64 ConnectionId) {
    Connections.erase(ConnectionId);
}

/**
 * @brief 一次 poll + 分发:监听 fd 上的可读触发 accept,连接 fd 上的事件触发收包/关闭。
 * @param TimeoutMs 传给 poll 的超时(毫秒)
 *
 * @detail 每次调用都把监听与连接重新拼进 pollfd 数组(本实现不做增量注册),
 *   因此是 O(监听数 + 连接数);分发顺序为监听在前、连接在后。
 */
void MNetEventLoop::RunOnce(int TimeoutMs) {
    TVector<pollfd>              PollFds;
    TVector<TPair<bool, uint64>> PollMeta;

    for (const SListener& L : Listeners) {
        if (!L.Handle.IsValid()) {
            continue;
        }
        pollfd P  = {};
        P.fd      = L.Handle.Get();
        P.events  = POLLIN;
        P.revents = 0;
        PollFds.push_back(P);
        PollMeta.push_back(TPair<bool, uint64>(true, L.Id));
    }

    for (auto& [Id, C] : Connections) {
        (void)Id;
        if (!C.Conn || !C.Conn->IsConnected()) {
            continue;
        }
        C.Conn->FlushSendBuffer();
        pollfd P  = {};
        P.fd      = C.Conn->GetSocketFd();
        P.events  = POLLIN;
        P.revents = 0;
        PollFds.push_back(P);
        PollMeta.push_back(TPair<bool, uint64>(false, C.Id));
    }

    if (PollFds.empty()) {
        // 无可 poll 的 fd(如多 Reactor 的 Sub 线程尚未派发任何连接):
        // 必须阻塞等待,否则调用方(MSubReactorPool 的 while 循环)会
        // 忙转吃满 CPU(曾实测 96 Sub 线程 ≈ 1500%)。
        if (TimeoutMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(TimeoutMs));
        } else {
            // 超时为 0 时不能 sleep(会被解读为"非阻塞"),用 yield 让出时间片兜底。
            std::this_thread::yield();
        }
        return;
    }

    const int N = poll(PollFds.data(), PollFds.size(), TimeoutMs);
    if (N < 0 || N == 0) {
        // N==0 是 poll 超时;N<0 表示被信号打断或参数错误。两者都无需分发。
        return;
    }

    // 需要摘除连接的 id 先攒在这里:遍历 poll 结果期间不动 Connections,
    // 避免回调内注册/注销导致迭代器失效或漏处理。
    TVector<uint64> ToClose;

    for (size_t i = 0; i < PollFds.size(); ++i) {
        if (PollFds[i].revents == 0) {
            continue;
        }
        const bool   bListener = PollMeta[i].first;
        const uint64 Id        = PollMeta[i].second;

        if (bListener) {
            for (SListener& L : Listeners) {
                if (L.Id != Id || !L.Handle.IsValid()) {
                    continue;
                }
                while (true) {
                    SAcceptedSocket Accepted = MSocket::AcceptConnection(L.Handle.Get());
                    if (!Accepted.IsValid()) {
                        break;
                    }

                    // 连接 id 在 reactor 线程分配,回调方无需再自己编号。
                    const uint64               ConnId = MUniqueIdGenerator::Generate();
                    TSharedPtr<INetConnection> Conn   = MakeShared<MTcpConnection>(std::move(Accepted.Socket), Accepted.RemoteAddress, Accepted.RemotePort);
                    if (L.OnAccept) {
                        L.OnAccept(ConnId, Conn);
                    }
                }
            }
            continue;
        }

        auto It = Connections.find(Id);
        if (It == Connections.end()) {
            continue;
        }
        SConnection& C = It->second;
        if (!C.Conn || !C.Conn->IsConnected()) {
            ToClose.push_back(Id);
            continue;
        }

        const short Rev = PollFds[i].revents;
        if ((Rev & (POLLERR | POLLHUP)) != 0) {
            if (C.OnClose) {
                C.OnClose(Id);
            }
            ToClose.push_back(Id);
            continue;
        }

        if ((Rev & POLLIN) != 0) {
            // ReceivePacket 一次调用切一个完整包,循环直到再无可切数据;
            // Packet 每轮复用同一缓冲,回调须自取所需(不可留存引用)。
            TByteArray Packet;
            while (C.Conn->ReceivePacket(Packet)) {
                if (C.OnRead) {
                    C.OnRead(Id, Packet);
                }
                Packet.clear();
            }

            // 回调可能把连接关掉了,此时补一次关闭通知。
            if (!C.Conn->IsConnected()) {
                if (C.OnClose) {
                    C.OnClose(Id);
                }
                ToClose.push_back(Id);
            }
        }
    }

    for (uint64 Id : ToClose) {
        Connections.erase(Id);
    }
}

/**
 * @brief 以固定 100ms 超时持续驱动 RunOnce(),直到 Stop()。
 *
 * @detail 因为没有可 poll 的 fd 时 RunOnce 也会休眠 100ms,本循环在空载下
 *   不会忙转;Stop() 只是清标志,退出发生在下一次循环判断。
 */
void MNetEventLoop::Run() {
    bRunning = true;
    while (bRunning) {
        RunOnce(100);
    }
}
