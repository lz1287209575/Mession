/**
 * @file MActorSystem.h
 * @brief MActorSystem - 全局 actor 运行时(进程单例).
 */
#pragma once

#include "Common/Runtime/Actor/MActorHandle.h"
#include "Common/Runtime/MLib.h"

#include <atomic>

class MSubReactorPool;

/**
 * @brief MActorSystem - 全局 actor 运行时.
 *
 * 职责:
 * 1. 注册 actor(IActor*),按 ActorId hash 选 Sub 分布
 * 2. Find ActorId → MActorHandle(任意线程可用)
 * 3. Shutdown 时回调 OnDestroyed
 *
 * actor 分布策略:hash(ActorId) % SubCount
 * 同一 ActorId 永远在同一 Sub(单线程访问 → 无锁安全)
 */
class MActorSystem {
    public:
    static MActorSystem& Get();

    /**
     * @brief Init - 启动期绑定 SubPool.
     * @param InSubPool Sub reactor 池(actor 会分布到各 Sub 上)
     */
    void Init(MSubReactorPool* InSubPool);

    /** @brief Shutdown - 销毁所有 actor + 清理. */
    void Shutdown();

    /**
     * @brief Register - 注册 actor.
     *
     * @param InActor  actor 共享指针(system 持一份,Handle 持副本)
     * @param InPreferredSubId 期望 SubId(默认 = UINT32_MAX = 系统按 hash(ActorId) 选)
     */
    void Register(TSharedPtr<IActor> InActor, uint32 InPreferredSubId = UINT32_MAX);

    /** @brief Unregister - 注销 actor + 销毁. */
    void Unregister(uint64 InActorId);

    /**
     * @brief Find - 拿 actor 句柄.
     * @return 本进程 → 有效 Handle;远端 → 仅 id 占位
     */
    MActorHandle Find(uint64 InActorId);

    /**
     * @brief DispatchLocal - 把消息 dispatch 到本进程内目标 actor 的 Sub 线程.
     *
     * 入口:MEchoService::OnActorMessage ServerCall(对端 RPC 把 actor 消息
     * 发到本进程后的接收端)。
     * 语义:找到 ActorId 对应的本地 actor,投递到其 Sub 的 TaskLoop,等
     * Sub 线程 drain 时调 actor->OnMessage(Msg) —— 保证 actor state 单线程访问。
     *
     * ReplyPromise 不在 wire 上,本函数内部不处理回包路径(Call 远端 TODO)。
     *
     * @return true 成功入 Sub 队列;false pending 超限（已 drop,server 端发投递失败 ack）
     */
    bool DispatchLocal(uint64 InActorId, const FActorMessage& InMsg);

    /**
     * @brief DispatchLocalWithStatus - DispatchLocal + 返回是否入队成功.
     *
     * 与 DispatchLocal 行为相同;这是给 server 端 DispatchActorPostMessage 用的——
     * 需要根据入队成败决定发 MT_ServerPush(delivered) 还是 MT_ServerPush(queue_full)。
     */
    bool DispatchLocalWithStatus(uint64 InActorId, const FActorMessage& InMsg) {
        return DispatchLocal(InActorId, InMsg);
    }

    /** @return 当前注册 actor 数. */
    uint32 GetActorCount() const;

    private:
    MActorSystem() = default;

    struct SActorEntry {
        TSharedPtr<IActor>  Actor;
        uint32             SubId = 0;
        // Per-actor pending count: Sub ambient.Post 时 ++,OnMessage 完成时 --。
        // 当 actor 处理慢于 Post 频率,计数累积;超过 kMaxPendingPerActor 时
        // 新到的 Post 直接 drop（避免 Sub 队列无限增长、actor 状态机雪崩）。
        TSharedPtr<std::atomic<uint32_t>> PendingCounter;

        // === 阶段 C:per-actor outbox 队列 ===
        // 跨进程 Post 失败时（连接断开/SendBuffer 满）→ 入队。
        // 容量上限 kMaxOutboxPerActor;满了就丢最早（FIFO 退化）。
        // MEndpointCache::OnEndpointChange 检测到 actor 端点恢复时 →
        // DrainActor() 把 outbox 里的消息按序重发。
        TQueue<TPair<EServerType, FActorMessage>> Outbox;

        // === 阶段 C 完善:at-least-once SequenceId ===
        // per-actor 单调递增计数器。MRpcChannel::SendActor 取号 +1,server 收到后
        // 回 MT_ServerPush ack 带相同 SequenceId,sender 收 ack → 从 outbox 删
        // 所有 < ack.SequenceId 的消息。
        TSharedPtr<std::atomic<uint64_t>> NextSequenceId;
    };

    MSubReactorPool*          SubPool = nullptr;
    mutable std::mutex        ActorsMutex;
    TMap<uint64, SActorEntry> LocalActors;

public:
    /** @brief Per-actor pending message count threshold (drop policy). */
    static constexpr uint32_t kMaxPendingPerActor = 1024;

    /** @brief Per-actor outbox 容量上限（跨连接 Post 缓冲）。 */
    static constexpr uint32_t kMaxOutboxPerActor = 1024;

    /** @brief Total dropped Post 计数（per-instance metric）—— 用于监控/告警。 */
    static std::atomic<uint64_t>& TotalDroppedPosts() {
        static std::atomic<uint64_t> Counter{0};
        return Counter;
    }

    /**
     * @brief EnqueueActorOutbox - 把失败 Post 入 outbox.
     *
     * 调用方:MActorHandle::Post 在 SendActor 失败时调用。
     * outbox 满则丢最早,记录 metric。
     *
     * @param InActorId  目标 actor id
     * @param InTarget   原要发送到的 ServerType
     * @param InMsg      失败的消息
     */
    void EnqueueActorOutbox(uint64 InActorId, EServerType InTarget, FActorMessage&& InMsg);

    /**
     * @brief DrainActorOutbox - 把 outbox 里的消息按序重发.
     *
     * 调用方:MEndpointCache::OnEndpointChange 检测到某 ServerType 重连时
     * 对该 ServerType 上所有 actor 调用一次本方法。
     *
     * @param InActorId  目标 actor id
     * @param InTarget   本次重连的 ServerType(只重发匹配该类型的消息)
     */
    void DrainActorOutbox(uint64 InActorId, EServerType InTarget);

    /**
     * @brief 触发对单个 actor 的 outbox drain(如果目标 ServerType 刚恢复).
     *
     * @param InActorId  目标 actor id
     * @param InNewTarget 刚恢复的 ServerType
     */
    static void OnActorEndpointRecovered(uint64 InActorId, EServerType InNewTarget);

    /**
     * @brief AllocateSequenceId - 给 actor 分配下一个 SequenceId(per-actor 单调递增).
     *
     * @return 分配到的 SequenceId;0 表示分配失败（actor 未注册）
     *
     * 线程安全:per-actor atomic fetch_add,多线程并发安全。
     */
    uint64 AllocateSequenceId(uint64 InActorId);
};