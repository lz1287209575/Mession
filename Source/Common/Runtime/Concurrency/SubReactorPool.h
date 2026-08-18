/**
 * @file SubReactorPool.h
 * @brief MSubReactorPool — 多 Reactor 池(N 个独立 NetEventLoop + TaskLoop + ambient).
 */
#pragma once

#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/EventLoop/NetEventLoop.h"
#include "Common/Runtime/MLib.h"

#include <atomic>
#include <thread>

class MTaskEventLoop;

/**
 * @brief MSubReactorPool - 多 Reactor 池.
 *
 * 持有 N 个独立的 I/O 线程 + TaskLoop 线程对。
 * fd 按 remote_addr hash 分桶,保证同一客户端始终在同一 Sub 上,
 * 保留 session affinity,提升 ConnectionPool 与业务 sharded cache 的命中率。
 *
 * 生命周期:
 *   1. Init(N) — 启动 N 个 Sub,各自跑独立线程
 *   2. PickSub / GetAmbient / GetLoop / Post — 业务调用
 *   3. Shutdown() — Stop 所有 Sub + join 线程
 *
 * 历史:MAsync namespace 待迁移到 mession::async(违反 CodingStyle §1.10)。
 */
class MSubReactorPool {
    public:
    MSubReactorPool() = default;
    ~MSubReactorPool();

    MSubReactorPool(const MSubReactorPool&)            = delete;
    MSubReactorPool& operator=(const MSubReactorPool&) = delete;

    /**
     * @brief Init - 启动 N 个 Sub reactor,各自跑在独立线程上.
     * @param InSubCount  Sub 数量,通常等于 std::thread::hardware_concurrency()
     */
    /**
     * @brief Start - 启动所有 Sub 线程(必须在 Init 之后、所有跨线程注册完成之后调用).
     */
    void Start();
    void Init(uint32 InSubCount);

    /**
     * @brief Shutdown - 通知所有 Sub 退出,join 线程.
     *
     * 重复调用安全(后续调用直接 return)。
     */
    void Shutdown();

    /**
     * @brief PickSub - 按 remote_addr hash 选 Sub.
     * @param InRemoteAddr  远端地址(含端口),保证同一连接总落在同一 Sub
     * @return  SubId ∈ [0, InSubCount)
     */
    uint32 PickSub(const MString& InRemoteAddr) const;

    /**
     * @brief PickSub - 按 ActorId 选 Sub(hash(ActorId) % SubCount).
     * @param InActorId  actor id
     * @return  SubId ∈ [0, InSubCount)
     */
    uint32 PickSub(uint64 InActorId) const;

    /**
     * @brief Post - 把任务投递到指定 Sub 的 TaskLoop,由该 Sub 的 I/O 线程执行.
     *
     * 跨线程访问 Sub 状态时必须用本接口,不能直接在主线程改 Sub 内部的 TMap。
     *
     * @param InSubId  目标 Sub id
     * @param InTask   任务(在 Sub I/O 线程执行)
     */
    void Post(uint32 InSubId, TFunction<void()> InTask);

    /**
     * @brief GetAmbient - 拿指定 Sub 的 ambient context,给 Worker 异步续体显式 Post 用.
     *
     * Worker 线程自己的 thread_local GCurrentContext 是 nullptr,
     * 异步续体必须显式拿目标 Sub 的 ambient 才能 Post 回 Sub 写包,
     * 否则跨线程改 SendBuffer 会数据竞争。
     *
     * @param InSubId  目标 Sub id
     * @return ambient context(生命周期随 pool);SubId 越界返 nullptr
     */
    MAsync::MAsyncContext* GetAmbient(uint32 InSubId) const;

    /**
     * @brief GetLoop - 拿指定 Sub 的 MNetEventLoop.
     *
     * 注意:跨线程访问 GetLoop()->Connections TMap 是禁止的。
     * 主 acceptor 只用 Post 让 Sub 自己改自己的 Connections。
     *
     * @param InSubId  目标 Sub id
     * @return Sub 的 MNetEventLoop(空指针 = 无效 SubId)
     */
    MNetEventLoop* GetLoop(uint32 InSubId) const;

    /** @return Sub 数量. */
    uint32 GetSubCount() const {
        return SubCount;
    }

    private:
    uint32                                     SubCount = 0;
    TVector<TSharedPtr<MNetEventLoop>>         SubLoops;
    TVector<TSharedPtr<MTaskEventLoop>>        SubTaskLoops;
    TVector<TSharedPtr<MAsync::MAsyncContext>> SubAmbients;
    TVector<TSharedPtr<std::thread>>           LoopThreads;
    std::atomic<bool>                          bInitialized{false};
    std::atomic<bool>                          bRunning{false};
};