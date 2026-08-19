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

    /**
     * @brief SOutboxEntry - per-actor outbox 中的一条待重发消息.
     *
     * SequenceId 用于 ack-based 删除:server 回 MT_ServerPush ack 带 SequenceId,
     * 客户端按 Seq 在 outbox 里找到对应 entry 删掉。
     * Target 记的是原始目标 ServerType,drain 时按此过滤（只重发到该 target）。
     * 同 ActorId 多个 endpoint（fan-out）每个 endpoint 一条 entry;
     * AckOutbox 会按 (SequenceId, Target) 匹配删除。
     */
    struct SOutboxEntry {
        EServerType   Target;
        uint64        SequenceId;
        FActorMessage Msg;
    };

    struct SActorEntry {
        TSharedPtr<IActor> Actor;
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
        // 收到 server 的 MT_ServerPush ack(ActorId, SequenceId) 时 AckOutbox 删对应 entry。
        TQueue<SOutboxEntry> Outbox;

        // === 阶段 C 完善:at-least-once SequenceId ===
        // per-actor 单调递增计数器。MRpcChannel::SendActor 取号 +1,server 收到后
        // 回 MT_ServerPush ack 带相同 SequenceId,sender 收 ack → 从 outbox 删
        // 所有 < ack.SequenceId 的消息。
        TSharedPtr<std::atomic<uint64_t>> NextSequenceId;
    };

    MSubReactorPool*          SubPool = nullptr;
    mutable std::mutex        ActorsMutex;
    TMap<uint64, SActorEntry> LocalActors;

    // 本地 actor 列表变化后全量上报 Registry（跨实例 actor 路由依赖 endpoint.ActorIds）。
    void NotifyRegistryActorChange();

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
     * 调用方:SendActor 失败时调用。
     * outbox 满则丢最早,记录 metric。
     *
     * @param InActorId  目标 actor id
     * @param InTarget   原要发送到的 ServerType
     * @param InMsg      失败的消息（Msg.SequenceId 已分配,用于 ack 匹配）
     */
    void EnqueueActorOutbox(uint64 InActorId, EServerType InTarget, FActorMessage&& InMsg);

    /**
     * @brief AckOutbox - server 确认投递后,删 outbox 里 <= AckedSeqId 的 entry.
     *
     * 调用方:MT_ServerPush handler(MEndpointCache::AttachDispatchToConnection switch case)
     * 解析 ack 拿到 ActorId + SequenceId,调本方法。
     *
     * 阈值 ≤ AckedSeqId 是为了处理 ack-in-flight 期间的乱序（虽然走同一条 TCP
     * 一般顺序,但极端 race 仍可能 ack 顺序与发送顺序不一致）。FIFO ack 简化为
     * ≤ 一次性删到 ack 之前所有 entry,后续 ack 命中时 outbox 应该已空。
     */
    void AckOutbox(uint64 InActorId, uint64 InAckedSeqId);

    /**
     * @brief AckOutboxByRoute - fan-out 场景专用:仅删 outbox 里特定 (Seq, Target) 的 entry.
     *
     * 区别于 AckOutbox:那个按 Seq 全删;这个按 (Seq, Target) 精确删 1:N fan-out 中
     * 某一 route 的 ack 确认。MT_ServerPush handler 应从 Sender 拿到连接 ServerType
     * 调本方法,而不是 AckOutbox —— 避免「一个 route 还没处理就被另一个 route 的
     * ack 删掉」的 at-most-once violation。
     */
    void AckOutboxByRoute(uint64 InActorId, uint64 InAckedSeqId, EServerType InTargetRoute);

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

    /**
     * @brief SendActor - 跨进程 actor Post 高层入口(分配 Seq + try send + outbox fallback).
     *
     * 流程:
     * 1) 调 AllocateSequenceId 给本次 Post 分配 Seq → 写入 InMsg.SequenceId
     * 2) 调 MRpcChannel::SendActor(InMsg) 真正写 wire
     * 3) 失败 → 调 EnqueueActorOutbox 入 outbox（带 SequenceId 用于 ack 匹配）
     *
     * 调用方:MActorHandle::Post 远端路径
     *
     * @return true 成功入 wire;false 入 outbox / actor 未注册
     */
    static bool SendActor(uint64 InActorId, FActorMessage InMsg);

    /**
     * @brief SaveActorState - 同步保存 actor 内部 state 到文件.
     *
     * 流程:
     * 1) 通过 ambient.Post 把"序列化"任务投递到 actor 自己的 Sub 线程
     * 2) Sub 线程上 actor->SerializeState() 执行（无锁读 state）
     * 3) 通过 MPromise 把结果回给调用线程
     * 4) 调用线程拿到 bytes 后写文件
     *
     * 安全性:SerializeState 在 actor Sub 线程跑（无锁读 actor state）;
     * 调用线程阻塞等 future;无锁竞争。
     *
     * @param InActorId 目标 actor
     * @param InFilePath 写入路径(覆盖式)
     * @return true 成功;false actor 不存在 / 序列化失败 / 文件写入失败
     */
    bool SaveActorState(uint64 InActorId, const MString& InFilePath);

    /**
     * @brief LoadActorState - 同步从文件恢复 actor 内部 state.
     *
     * 流程:
     * 1) 主线程读文件 → bytes
     * 2) 通过 ambient.Post 把"反序列化"任务投递到 actor Sub 线程
     * 3) Sub 线程上 actor->RestoreState() 执行(直接写 actor state)
     * 4) 通过 MPromise 把结果(bool)回给调用线程
     *
     * 安全性:RestoreState 在 actor Sub 线程跑,直接写 state,无锁。
     * 业务应保证:Restore 期间 actor 不会收到 OnMessage(MActorSystem 不保证,
     * 业务自己保证时序;PoC 阶段 EchoService::OnRunStarted 里就 actor 注册前
     * 完成 restore,避开 race)。
     *
     * @return true 成功;false actor 不存在 / 文件不存在 / RestoreState 失败
     */
    bool LoadActorState(uint64 InActorId, const MString& InFilePath);

    // === MT_ServerPush listener 机制（业务订阅投递状态 / 自定义 server push）===
    //
    // 注册的 listener 会在每次收到 MT_ServerPush 包时调,参数是解析后的
    // (StatusCode, ActorId, SequenceId)。listener 在 EndpointCache 收到包时
    // 同步调,业务方负责 listener 的线程安全（典型用法:加 metric、写 log、ack 业务等）。
    //
    // 与 MActorSystem::AckOutbox 互补:AckOutbox 是 actor 内部 outbox 维护;
    // listener 是业务可观测性 + 业务自定义 server-push 通知。
    using FServerPushListener = TFunction<void(uint8 StatusCode, uint64 ActorId, uint64 SequenceId)>;
    using HServerPushListener = uint64; // 0 = 无效 handle

    HServerPushListener RegisterServerPushListener(FServerPushListener InListener);
    void                UnregisterServerPushListener(HServerPushListener InHandle);
    void                FireServerPushListeners(uint8 InStatusCode, uint64 InActorId, uint64 InSequenceId);
};