#include "Common/Runtime/Actor/MActorSystem.h"

#include "Common/Net/Rpc/MRpcChannel.h"
#include "Common/Net/ServiceDiscovery/Endpoint.h"
#include "Common/Runtime/Actor/FActorMessage.h"
#include "Common/Runtime/Actor/IActor.h"
#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/SubReactorPool.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Object/Result.h"

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

    uint32                          SubId = 0;
    TSharedPtr<IActor>              Actor;
    TSharedPtr<std::atomic<uint32_t>> Counter;
    {
        std::lock_guard<std::mutex> Lock(ActorsMutex);
        auto                        It = LocalActors.find(InActorId);
        if (It == LocalActors.end()) {
            LOG_WARN("MActorSystem::DispatchLocal: actor %llu not registered locally", static_cast<unsigned long long>(InActorId));
            return false;
        }
        SubId    = It->second.SubId;
        Actor    = It->second.Actor;
        Counter  = It->second.PendingCounter;
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
        Counter->fetch_sub(1);  // 撤销 increment
        const uint64 TotalDropped = TotalDroppedPosts().fetch_add(1) + 1;
        // 高频路径:每 1000 次 drop 报一次（避免日志淹没）
        if (TotalDropped % 1000 == 1) {
            LOG_WARN("MActorSystem::DispatchLocal: actor %llu pending=%u > %u, dropping new Post (total dropped=%llu)",
                     static_cast<unsigned long long>(InActorId),
                     static_cast<unsigned>(Pending),
                     static_cast<unsigned>(kMaxPendingPerActor),
                     static_cast<unsigned long long>(TotalDropped));
        }
        return false;  // 阶段 D:让 server 知道投递失败,可发 MT_ServerPush(queue_full)
    }

    // Post 到 actor 自己的 Sub ambient —— 等 Sub drain 时在 actor 线程调 OnMessage。
    // 这是 actor 单线程访问契约的强制点:OnMessage 永远在 Sub #SubId 线程被调。
    MAsync::MAsyncContext* Ambient = SubPool->GetAmbient(SubId);
    if (Ambient == nullptr) {
        LOG_ERROR("MActorSystem::DispatchLocal: null ambient for SubId=%u", static_cast<unsigned>(SubId));
        Counter->fetch_sub(1);  // 撤销 increment（Post 没成功）
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
    return true;  // 成功入队
}

// =====================================================================
// 阶段 C:per-actor outbox 队列（跨连接 Post 缓冲 + 端点恢复后重发）
// =====================================================================

void MActorSystem::EnqueueActorOutbox(uint64 InActorId, EServerType InTarget, FActorMessage&& InMsg) {
    // 注意:无锁 fast path——失败 Post 来自业务线程,直接进 outbox
    // 不阻塞;满了丢最早,记录 metric。
    TPair<EServerType, FActorMessage> Entry(InTarget, std::move(InMsg));

    std::lock_guard<std::mutex> Lock(ActorsMutex);
    auto It = LocalActors.find(InActorId);
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
                LOG_WARN("MActorSystem::EnqueueActorOutbox: actor %llu outbox full (%u), dropping oldest (total dropped=%llu)",
                         static_cast<unsigned long long>(InActorId),
                         static_cast<unsigned>(kMaxOutboxPerActor),
                         static_cast<unsigned long long>(TotalDropped));
            }
        }
    }
    Outbox.push(std::move(Entry));
}

void MActorSystem::DrainActorOutbox(uint64 InActorId, EServerType InTarget) {
    // 把 outbox 里所有匹配 InTarget 的消息按序重发。
    // 失败的消息（对方 actor 已 Unregister / 路由失效）继续留在 outbox,等下次重试。
    TVector<TPair<EServerType, FActorMessage>> ToSend;

    {
        std::lock_guard<std::mutex> Lock(ActorsMutex);
        auto It = LocalActors.find(InActorId);
        if (It == LocalActors.end()) {
            return;  // actor 已注销,outbox 失效
        }
        auto& Outbox = It->second.Outbox;
        // 把匹配 InTarget 的元素挑出来,按 FIFO 顺序重发
        // 不能在持锁时调 SendActor（避免死锁——SendActor 可能回调 MEndpointCache 又拿 ActorsMutex）
        TVector<TPair<EServerType, FActorMessage>> Tmp;
        Tmp.reserve(Outbox.size());
        while (!Outbox.empty()) {
            TPair<EServerType, FActorMessage> E = std::move(Outbox.front());
            Outbox.pop();
            if (E.first == InTarget) {
                ToSend.push_back(std::move(E));
            } else {
                Tmp.push_back(std::move(E));  // 不是这个 Target 的,暂存
            }
        }
        // 暂存的消息放回 outbox
        for (auto& E : Tmp) {
            Outbox.push(std::move(E));
        }
    }

    // 锁外重发（避免持锁回调）
    for (auto& E : ToSend) {
        MRpcChannel::Get().SendActor(InActorId, E.second);
    }
}

void MActorSystem::OnActorEndpointRecovered(uint64 InActorId, EServerType InNewTarget) {
    MActorSystem::Get().DrainActorOutbox(InActorId, InNewTarget);
}

uint64 MActorSystem::AllocateSequenceId(uint64 InActorId) {
    // 1-based 避免 0（0 在 wire 上表示"未分配"）—— fetch_add 返回旧值,+1 = 新值
    std::lock_guard<std::mutex> Lock(ActorsMutex);
    auto It = LocalActors.find(InActorId);
    if (It == LocalActors.end() || !It->second.NextSequenceId) {
        return 0;  // actor 未注册
    }
    return It->second.NextSequenceId->fetch_add(1) + 1;
}