/**
 * @file TaskEventLoop.h
 * @brief MTaskEventLoop —— 单线程任务队列,同时实现 IExecutor 与 IEventLoop。
 *
 * @detail 语义:纯任务事件循环,不做任何 I/O poll。Post() 可从任意线程调用,
 *   RunOnce() 在"执行线程"上把队列整体摘下依次执行。多 Reactor 架构下每个
 *   Sub 线程各持有一个本对象,作为续体执行上下文(见 Async/IExecutor.h 说明)。
 */
#pragma once

#include "Common/Runtime/Async/IExecutor.h"
#include "Common/Runtime/EventLoop/IEventLoop.h"
#include "Common/Runtime/MLib.h"

/**
 * @brief MTaskEventLoop - 纯任务事件循环(无 I/O)。
 *
 * @detail 线程模型:
 *   1. Post():任意线程可调,内部加锁,只入队不执行
 *   2. RunOnce():由持有它的线程反复调用;进入时把 OwnerThreadId 刷新为调用线程
 *   3. 出队采用"先 swap 再执行",任务体在锁外运行——否则任务内部再次 Post
 *      会自锁
 */
class MTaskEventLoop : public mession::async::IExecutor, public IEventLoop {
    public:
    /**
     * @brief Post - 投递任务,任意线程可调。
     * @param Task 待执行任务;TTask 为空时静默丢弃
     */
    void Post(TTask Task) override;

    /**
     * @brief IsCurrentThread - 当前线程是否为本循环的执行线程。
     * @return true 当前线程 id 与 OwnerThreadId 相同;RunOnce 尚未进入过时恒为 false
     */
    bool IsCurrentThread() const override;

    /**
     * @brief RunOnce - 刷新执行线程标记,并执行当前队列中的全部任务。
     * @param timeoutMs 未使用:纯任务循环不需要轮询超时
     */
    void RunOnce(int timeoutMs = 0) override;

    private:
    TDeque<TTask>      PendingTasks; // 由 TaskMutex 保护
    mutable std::mutex TaskMutex;
    TAtomic<MThreadId> OwnerThreadId{}; // 默认空 id(std::thread::id{});RunOnce 进入时固化为调用线程
};
