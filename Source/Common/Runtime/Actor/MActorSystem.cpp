#include "Common/Runtime/Actor/MActorSystem.h"

#include "Common/Net/Rpc/MRpcChannel.h"
#include "Common/Net/ServiceDiscovery/Endpoint.h"
#include "Common/Runtime/Actor/FActorMessage.h"
#include "Common/Runtime/Actor/IActor.h"
#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Concurrency/SubReactorPool.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Object/Result.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>

MActorSystem& MActorSystem::Get() {
    static MActorSystem Instance;
    return Instance;
}

void MActorSystem::Init(MSubReactorPool* InSubPool) {
    if (InSubPool == nullptr) {
        LOG_FATAL("MActorSystem::Init called with null SubPool");
        std::abort();
    }
    SubPool = InSubPool;
    LOG_INFO("MActorSystem initialized with %u subs", SubPool->GetSubCount());
}

void MActorSystem::Shutdown() {
    std::lock_guard<std::mutex> Lock(ActorsMutex);
    for (auto& Pair : LocalActors) {
        if (Pair.second.Actor != nullptr) {
            Pair.second.Actor->OnDestroyed();
        }
    }
    LocalActors.clear();
    SubPool = nullptr;
    LOG_INFO("MActorSystem shutdown complete");
}

void MActorSystem::Register(TSharedPtr<IActor> InActor, uint32 InPreferredSubId) {
    if (InActor == nullptr) {
        LOG_FATAL("MActorSystem::Register called with null actor");
        std::abort();
    }

    const uint64 ActorId  = InActor->GetActorId();
    const uint32 SubCount = SubPool != nullptr ? SubPool->GetSubCount() : 0;
    if (SubCount == 0) {
        LOG_FATAL("MActorSystem::Register before Init or SubCount is 0");
        std::abort();
    }

    // 选 Sub:优先 PreferredSubId,否则 hash(ActorId) % SubCount
    uint32 SubId = InPreferredSubId;
    if (SubId == UINT32_MAX || SubId >= SubCount) {
        SubId = static_cast<uint32>(ActorId % SubCount);
    }

    {
        std::lock_guard<std::mutex> Lock(ActorsMutex);
        if (LocalActors.find(ActorId) != LocalActors.end()) {
            LOG_WARN("actor %llu already registered, replacing", static_cast<unsigned long long>(ActorId));
            LocalActors[ActorId].Actor->OnDestroyed();
        }
        SActorEntry Entry;
        Entry.Actor          = InActor; // TSharedPtr 副本,system 与 caller 共享
        Entry.SubId          = SubId;
        Entry.PendingCounter = MakeShared<std::atomic<uint32_t>>(0);
        Entry.NextSequenceId = MakeShared<std::atomic<uint64_t>>(0);
        LocalActors[ActorId] = std::move(Entry);
    }

    // 在 actor 自己的 Sub 线程回调 OnCreated
    MAsync::MAsyncContext* Ambient = SubPool->GetAmbient(SubId);
    if (Ambient != nullptr) {
        Ambient->Post([InActor] { InActor->OnCreated(); });
    }

    LOG_INFO("actor %llu registered to SubId=%u", static_cast<unsigned long long>(ActorId), static_cast<unsigned>(SubId));
}

void MActorSystem::Unregister(uint64 InActorId) {
    TSharedPtr<IActor> ReleasedActor;
    {
        std::lock_guard<std::mutex> Lock(ActorsMutex);
        auto                        It = LocalActors.find(InActorId);
        if (It == LocalActors.end()) {
            LOG_WARN("actor %llu not registered", static_cast<unsigned long long>(InActorId));
            return;
        }
        ReleasedActor = It->second.Actor;
        LocalActors.erase(InActorId);
    }
    if (ReleasedActor != nullptr) {
        ReleasedActor->OnDestroyed();
    }
}

MActorHandle MActorSystem::Find(uint64 InActorId) {
    std::lock_guard<std::mutex> Lock(ActorsMutex);
    auto                        It = LocalActors.find(InActorId);
    if (It == LocalActors.end()) {
        // 远端 actor — 当前 stub,后续接 RPC
        return MActorHandle(InActorId);
    }
    TSharedPtr<IActor> Actor = It->second.Actor;
    return MActorHandle(Actor, SubPool);
}

uint32 MActorSystem::GetActorCount() const {
    std::lock_guard<std::mutex> Lock(ActorsMutex);
    return static_cast<uint32>(LocalActors.size());
}

bool MActorSystem::DispatchLocal(uint64 InActorId, const FActorMessage& InMsg) {
    if (SubPool == nullptr) {
        LOG_ERROR("MActorSystem::DispatchLocal before Init");
        return false;
    }

    uint32                            SubId = 0;
    TSharedPtr<IActor>                Actor;
    TSharedPtr<std::atomic<uint32_t>> Counter;
    {
        std::lock_guard<std::mutex> Lock(ActorsMutex);
        auto                        It = LocalActors.find(InActorId);
        if (It == LocalActors.end()) {
            LOG_WARN("MActorSystem::DispatchLocal: actor %llu not registered locally", static_cast<unsigned long long>(InActorId));
            return false;
        }
        SubId   = It->second.SubId;
        Actor   = It->second.Actor;
        Counter = It->second.PendingCounter;
    }

    if (!Actor || !Counter) {
        LOG_WARN("MActorSystem::DispatchLocal: null actor/counter for %llu", static_cast<unsigned long long>(InActorId));
        return false;
    }

    // === 阶段 B:per-actor pending 上限 + drop policy ===
    // 当 actor 处理慢于 Post 频率,pending 累积;超过 kMaxPendingPerActor 时
    // 新到的 Post 直接 drop（避免 Sub ambient 队列无限增长、actor 状态机雪崩）。
    // 注意:drop 是"丢新",不是"丢老"——后者需要维护 per-actor 的局部队列,复杂度高,
    // PoC 阶段"丢新"已足够保护关键路径（业务 thread 永远不会因为某个慢 actor 卡住）。
    const uint32 Pending = Counter->fetch_add(1) + 1;
    if (Pending > kMaxPendingPerActor) {
        Counter->fetch_sub(1); // 撤销 increment
        const uint64 TotalDropped = TotalDroppedPosts().fetch_add(1) + 1;
        // 高频路径:每 1000 次 drop 报一次（避免日志淹没）
        if (TotalDropped % 1000 == 1) {
            LOG_WARN("MActorSystem::DispatchLocal: actor %llu pending=%u > %u, dropping new Post (total dropped=%llu)", static_cast<unsigned long long>(InActorId), static_cast<unsigned>(Pending), static_cast<unsigned>(kMaxPendingPerActor),
                     static_cast<unsigned long long>(TotalDropped));
        }
        return false; // 阶段 D:让 server 知道投递失败,可发 MT_ServerPush(queue_full)
    }

    // Post 到 actor 自己的 Sub ambient —— 等 Sub drain 时在 actor 线程调 OnMessage。
    // 这是 actor 单线程访问契约的强制点:OnMessage 永远在 Sub #SubId 线程被调。
    MAsync::MAsyncContext* Ambient = SubPool->GetAmbient(SubId);
    if (Ambient == nullptr) {
        LOG_ERROR("MActorSystem::DispatchLocal: null ambient for SubId=%u", static_cast<unsigned>(SubId));
        Counter->fetch_sub(1); // 撤销 increment（Post 没成功）
        return false;
    }

    // 拷贝消息避免 caller 释放;lambda 捕获 shared_ptr 保活直到 OnMessage 返回。
    // Counter 也在 lambda 里:OnMessage 完成后 fetch_sub 释放 pending 槽位。
    FActorMessage Captured = InMsg;
    Ambient->Post([Actor, Counter, Captured]() mutable {
        Actor->OnMessage(Captured);
        // OnMessage 完成后释放 pending 槽位（即使 actor 内部抛异常,也保证释放——
        // 这点 std::terminate 不保证,所以这里如果未来需要严格保证,可以包 try/catch）
        Counter->fetch_sub(1);
    });
    return true; // 成功入队
}

// =====================================================================
// 阶段 C:per-actor outbox 队列（跨连接 Post 缓冲 + 端点恢复后重发）
// =====================================================================

void MActorSystem::EnqueueActorOutbox(uint64 InActorId, EServerType InTarget, FActorMessage&& InMsg) {
    // 注意:无锁 fast path——失败 Post 来自业务线程,直接进 outbox
    // 不阻塞;满了丢最早,记录 metric。
    // 保留 InMsg.SequenceId 以便 AckOutbox 找到对应 entry 删除。
    SOutboxEntry Entry;
    Entry.Target     = InTarget;
    Entry.SequenceId = InMsg.SequenceId;
    Entry.Msg        = std::move(InMsg);

    std::lock_guard<std::mutex> Lock(ActorsMutex);
    auto                        It = LocalActors.find(InActorId);
    if (It == LocalActors.end()) {
        // actor 已被 Unregister 掉 → 静默丢
        return;
    }
    auto& Outbox = It->second.Outbox;
    if (Outbox.size() >= kMaxOutboxPerActor) {
        // 满了丢最早（FIFO 退化——前面的消息比新的"老",理论上更应该被保留;
        // 但 PoC 阶段丢新也能起到降级保护作用。改成丢最早只需把 front() 拿出来）
        if (!Outbox.empty()) {
            Outbox.pop();
            const uint64 TotalDropped = TotalDroppedPosts().fetch_add(1) + 1;
            if (TotalDropped % 1000 == 1) {
                LOG_WARN("MActorSystem::EnqueueActorOutbox: actor %llu outbox full (%u), dropping oldest (total dropped=%llu)", static_cast<unsigned long long>(InActorId), static_cast<unsigned>(kMaxOutboxPerActor),
                         static_cast<unsigned long long>(TotalDropped));
            }
        }
    }
    Outbox.push(std::move(Entry));
}

void MActorSystem::AckOutbox(uint64 InActorId, uint64 InAckedSeqId) {
    // 收到 server 投递确认 → 从 outbox 删所有 Seq ≤ InAckedSeqId 的 entry。
    // 用 ≤ 而不是 == 处理 ack-in-flight 期间的乱序（虽然走同一条 TCP 一般顺序,
    // 但极端 race 下 ack 仍可能与发送顺序不一致）。FIFO ack 简化为 ≤ 一次删到
    // ack 之前所有 entry；后续 ack 命中时 outbox 应该已空。
    if (InAckedSeqId == 0)
        return; // 0 = 未分配,无意义

    std::lock_guard<std::mutex> Lock(ActorsMutex);
    auto                        It = LocalActors.find(InActorId);
    if (It == LocalActors.end()) {
        return; // actor 已注销
    }
    auto& Outbox = It->second.Outbox;

    // TQueue 没有 iterator erase;做法:把所有 ≤ 的元素从队头取出,其余 push 回队尾
    // 由于 TQueue 内部是 std::deque,pop_front 一次一个
    TVector<SOutboxEntry> ToKeep;
    uint32                AckedCount = 0;
    while (!Outbox.empty()) {
        SOutboxEntry E = std::move(Outbox.front());
        Outbox.pop();
        if (E.SequenceId != 0 && E.SequenceId <= InAckedSeqId) {
            ++AckedCount; // 已确认 → 丢弃
        } else {
            ToKeep.push_back(std::move(E)); // 还没确认 → 保留
        }
    }
    // 把保留的 push 回去
    for (auto& E : ToKeep) {
        Outbox.push(std::move(E));
    }
    if (AckedCount > 0) {
        LOG_DEBUG("MActorSystem::AckOutbox: actor %llu acked %u entries (up to seq %llu)", static_cast<unsigned long long>(InActorId), static_cast<unsigned>(AckedCount), static_cast<unsigned long long>(InAckedSeqId));
    }
}

void MActorSystem::AckOutboxByRoute(uint64 InActorId, uint64 InAckedSeqId, EServerType InTargetRoute) {
    // fan-out 场景专用:仅删 outbox 里特定 (Seq, TargetRoute) 的 entry,不影响其他 route。
    // 这是 at-least-once 的关键 —— 一个 route 的 ack 不应让另一个 route 的 entry
    // 被删除,否则另一个 route 重启后漏消息。
    if (InAckedSeqId == 0)
        return;

    std::lock_guard<std::mutex> Lock(ActorsMutex);
    auto                        It = LocalActors.find(InActorId);
    if (It == LocalActors.end())
        return;
    auto& Outbox = It->second.Outbox;

    TVector<SOutboxEntry> ToKeep;
    uint32                AckedCount = 0;
    while (!Outbox.empty()) {
        SOutboxEntry E = std::move(Outbox.front());
        Outbox.pop();
        if (E.SequenceId == InAckedSeqId && E.Target == InTargetRoute) {
            ++AckedCount; // 该 route 该 seq 已确认
        } else {
            ToKeep.push_back(std::move(E));
        }
    }
    for (auto& E : ToKeep) {
        Outbox.push(std::move(E));
    }
    if (AckedCount > 0) {
        LOG_DEBUG("MActorSystem::AckOutboxByRoute: actor %llu seq %llu target %u acked", static_cast<unsigned long long>(InActorId), static_cast<unsigned long long>(InAckedSeqId), static_cast<unsigned>(InTargetRoute));
    }
}

void MActorSystem::DrainActorOutbox(uint64 InActorId, EServerType InTarget) {
    // 把 outbox 里所有匹配 InTarget 的消息按序重发。
    // 失败的消息（对方 actor 已 Unregister / 路由失效）继续留在 outbox,等下次重试。
    TVector<SOutboxEntry> ToSend;

    {
        std::lock_guard<std::mutex> Lock(ActorsMutex);
        auto                        It = LocalActors.find(InActorId);
        if (It == LocalActors.end()) {
            return; // actor 已注销,outbox 失效
        }
        auto& Outbox = It->second.Outbox;
        // 把匹配 InTarget 的元素挑出来,按 FIFO 顺序重发
        // 不能在持锁时调 SendActor（避免死锁——SendActor 可能回调 MEndpointCache 又拿 ActorsMutex）
        TVector<SOutboxEntry> Tmp;
        Tmp.reserve(Outbox.size());
        while (!Outbox.empty()) {
            SOutboxEntry E = std::move(Outbox.front());
            Outbox.pop();
            if (E.Target == InTarget) {
                ToSend.push_back(std::move(E));
            } else {
                Tmp.push_back(std::move(E)); // 不是这个 Target 的,暂存
            }
        }
        // 暂存的消息放回 outbox
        for (auto& E : Tmp) {
            Outbox.push(std::move(E));
        }
    }

    // 锁外重发（避免持锁回调）
    for (auto& E : ToSend) {
        MRpcChannel::Get().SendActor(InActorId, E.Msg);
    }
}

bool MActorSystem::SendActor(uint64 InActorId, FActorMessage InMsg) {
    // 1) 分配 SequenceId（写入 InMsg.SequenceId,outbox 用它做 ack 匹配）
    InMsg.SequenceId = MActorSystem::Get().AllocateSequenceId(InActorId);
    if (InMsg.SequenceId == 0) {
        // actor 未注册,业务侧 bug 或 race;直接 fail
        return false;
    }

    // 2) 1:N fan-out — 取 actor 全部 route（本地 + 远程），本地由 MActorHandle::Post
    // 在更上层处理（直接 Sub ambient 派发），这里只处理远端
    TVector<SActorRoute> AllRoutes = MActorRouter::Get().FindAllActorRoutes(InActorId);

    // 3) 每个远端 route 独立 send。失败 → 该 route 的 outbox entry 独立存（per-route at-least-once）。
    bool          bAnySucceeded = false;
    MActorSystem& Sys           = MActorSystem::Get();
    for (const SActorRoute& Route : AllRoutes) {
        if (Route.ServerType == EServerType::Unknown) {
            // 本地 route → MActorHandle::Post 走 ProcessLocal,这里跳过
            continue;
        }
        auto Connection = MEndpointCache::Get().GetOrConnect(Route.ServerType);
        if (!Connection || !Connection->IsConnected()) {
            // 失败 → outbox（每个失败 route 独立 entry;需要 Msg copy,因为后续 route 还要用）
            FActorMessage MsgCopy = InMsg;
            Sys.EnqueueActorOutbox(InActorId, Route.ServerType, std::move(MsgCopy));
            continue;
        }
        // 尝试 send（复用底层 wire 构造）
        if (MRpcChannel::Get().SendActor(InActorId, InMsg)) {
            bAnySucceeded = true;
        } else {
            FActorMessage MsgCopy = InMsg;
            Sys.EnqueueActorOutbox(InActorId, Route.ServerType, std::move(MsgCopy));
        }
    }

    // 没有 route（actor 全是 local 或全未注册）→ 算 fail
    return bAnySucceeded;
}

// =====================================================================
// 持久化:Save/Load actor 内部 state 到文件
// =====================================================================

bool MActorSystem::SaveActorState(uint64 InActorId, const MString& InFilePath) {
    // 1) 找到 actor 和它的 SubId
    TSharedPtr<IActor>     Actor;
    uint32                 SubId   = 0;
    MAsync::MAsyncContext* Ambient = nullptr;
    {
        std::lock_guard<std::mutex> Lock(ActorsMutex);
        auto                        It = LocalActors.find(InActorId);
        if (It == LocalActors.end() || SubPool == nullptr) {
            return false;
        }
        Actor   = It->second.Actor;
        SubId   = It->second.SubId;
        Ambient = SubPool->GetAmbient(SubId);
    }
    if (!Actor || !Ambient) {
        return false;
    }

    // 2) 在 Sub 线程跑 SerializeState(无锁读 actor state),用 Promise 等结果
    auto Promise = MakeShared<MPromise<TByteArray>>();
    auto Future  = Promise->GetFuture();
    Ambient->Post([Actor, Promise]() mutable {
        const TByteArray State = Actor->SerializeState();
        Promise->SetValue(std::move(State));
    });
    TByteArray State = Future.Get();

    // 3) 主线程写文件(binary 覆盖式)
    FILE* F = std::fopen(InFilePath.c_str(), "wb");
    if (F == nullptr) {
        LOG_ERROR("MActorSystem::SaveActorState: fopen(%s) failed", InFilePath.c_str());
        return false;
    }
    const size_t Written = std::fwrite(State.data(), 1, State.size(), F);
    std::fclose(F);
    if (Written != State.size()) {
        LOG_ERROR("MActorSystem::SaveActorState: short write %zu/%zu", Written, State.size());
        return false;
    }
    LOG_INFO("MActorSystem::SaveActorState: actor %llu -> %s (%zu bytes)", static_cast<unsigned long long>(InActorId), InFilePath.c_str(), State.size());
    return true;
}

bool MActorSystem::LoadActorState(uint64 InActorId, const MString& InFilePath) {
    // 1) 读文件
    FILE* F = std::fopen(InFilePath.c_str(), "rb");
    if (F == nullptr) {
        // 文件不存在不算错（首次启动、actor 未持久化）—— 不算 Load 失败
        LOG_DEBUG("MActorSystem::LoadActorState: %s not found, skip", InFilePath.c_str());
        return true;
    }
    std::fseek(F, 0, SEEK_END);
    const long Size = std::ftell(F);
    std::fseek(F, 0, SEEK_SET);
    if (Size < 0) {
        std::fclose(F);
        return false;
    }
    TByteArray   State(static_cast<size_t>(Size));
    const size_t Read = std::fread(State.data(), 1, State.size(), F);
    std::fclose(F);
    if (Read != State.size()) {
        LOG_ERROR("MActorSystem::LoadActorState: short read %zu/%zu", Read, State.size());
        return false;
    }

    // 2) 找到 actor 和 SubId
    TSharedPtr<IActor>     Actor;
    uint32                 SubId   = 0;
    MAsync::MAsyncContext* Ambient = nullptr;
    {
        std::lock_guard<std::mutex> Lock(ActorsMutex);
        auto                        It = LocalActors.find(InActorId);
        if (It == LocalActors.end() || SubPool == nullptr) {
            return false;
        }
        Actor   = It->second.Actor;
        SubId   = It->second.SubId;
        Ambient = SubPool->GetAmbient(SubId);
    }
    if (!Actor || !Ambient) {
        return false;
    }

    // 3) 在 Sub 线程跑 RestoreState(直接写 actor state)
    auto Promise = MakeShared<MPromise<bool>>();
    auto Future  = Promise->GetFuture();
    Ambient->Post([Actor, State = std::move(State), Promise]() mutable {
        const bool Ok = Actor->RestoreState(State);
        Promise->SetValue(Ok);
    });
    const bool Ok = Future.Get();
    if (!Ok) {
        LOG_WARN("MActorSystem::LoadActorState: actor %llu RestoreState returned false", static_cast<unsigned long long>(InActorId));
    }
    return Ok;
}

void MActorSystem::OnActorEndpointRecovered(uint64 InActorId, EServerType InNewTarget) {
    MActorSystem::Get().DrainActorOutbox(InActorId, InNewTarget);
}

uint64 MActorSystem::AllocateSequenceId(uint64 InActorId) {
    // 1-based 避免 0（0 在 wire 上表示"未分配"）—— fetch_add 返回旧值,+1 = 新值
    std::lock_guard<std::mutex> Lock(ActorsMutex);
    auto                        It = LocalActors.find(InActorId);
    if (It == LocalActors.end() || !It->second.NextSequenceId) {
        return 0; // actor 未注册
    }
    return It->second.NextSequenceId->fetch_add(1) + 1;
}

// =====================================================================
// MT_ServerPush listener 机制
// =====================================================================

namespace {
    // listener 存储（独立 mutex,避免与 actors 锁竞争）
    struct SListenerRegistry {
        std::mutex                                      Mutex;
        TMap<uint64, MActorSystem::FServerPushListener> Listeners;
        uint64                                          NextHandle = 1; // 0 = 无效
    };
    SListenerRegistry& ListenerRegistry() {
        static SListenerRegistry R;
        return R;
    }
} // namespace

MActorSystem::HServerPushListener MActorSystem::RegisterServerPushListener(FServerPushListener InListener) {
    if (!InListener) {
        return 0; // 无效 callback
    }
    auto&                       Reg = ListenerRegistry();
    std::lock_guard<std::mutex> Lock(Reg.Mutex);
    const HServerPushListener   Handle = Reg.NextHandle++;
    Reg.Listeners[Handle]              = std::move(InListener);
    return Handle;
}

void MActorSystem::UnregisterServerPushListener(HServerPushListener InHandle) {
    if (InHandle == 0)
        return;
    auto&                       Reg = ListenerRegistry();
    std::lock_guard<std::mutex> Lock(Reg.Mutex);
    Reg.Listeners.erase(InHandle);
}

void MActorSystem::FireServerPushListeners(uint8 InStatusCode, uint64 InActorId, uint64 InSequenceId) {
    // 拷贝 listener 列表到本地（避免持锁调业务代码 —— 业务 listener 可能回调
    // RegisterServerPushListener 形成死锁）
    TVector<FServerPushListener> Snapshot;
    {
        auto&                       Reg = ListenerRegistry();
        std::lock_guard<std::mutex> Lock(Reg.Mutex);
        Snapshot.reserve(Reg.Listeners.size());
        for (auto& KV : Reg.Listeners) {
            Snapshot.push_back(KV.second);
        }
    }
    for (auto& L : Snapshot) {
        if (L) {
            L(InStatusCode, InActorId, InSequenceId);
        }
    }
}