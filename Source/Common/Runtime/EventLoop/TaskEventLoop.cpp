/**
 * @file TaskEventLoop.cpp
 * @brief MTaskEventLoop 实现:加锁入队 + 摘链后在锁外执行。
 *
 * @detail 互斥范围只覆盖队列操作:RunOnce 先把整个队列 swap 到局部变量再释放
 *   锁,任务体在锁外运行,因此任务内部重入 Post() 不会自锁。
 */
#include "Common/Runtime/EventLoop/TaskEventLoop.h"

/** @brief Post - 入队;Task 为空时直接返回,不入队。 */
void MTaskEventLoop::Post(TTask Task) {
    if (!Task) {
        return;
    }
    std::lock_guard<std::mutex> Lock(TaskMutex);
    PendingTasks.push_back(std::move(Task));
}

/**
 * @brief IsCurrentThread - 比较当前线程与已固化的执行线程。
 * @return true 两者相同;OwnerThreadId 仍为默认空 id 时返回 false
 *
 * @detail 使用 acquire 读取,与 RunOnce 中的 release 写入配对,确保不会读到
 *   半写状态;本函数不加 TaskMutex,可在任意线程调用。
 */
bool MTaskEventLoop::IsCurrentThread() const {
    return std::this_thread::get_id() == OwnerThreadId.load(std::memory_order_acquire);
}

/**
 * @brief RunOnce - 固化执行线程,然后把当前队列里的任务全部执行一遍。
 * @param timeoutMs 未使用:定义处形参为占位,纯任务循环不需要超时
 *
 * @detail 每次进入都会刷新 OwnerThreadId;实践中调用线程固定,因此等价于
 *   首次进入时的一次性固化。队列在锁内 swap 出来,随后在锁外执行,
 *   RunOnce 期间新 Post 的任务留到下一次 RunOnce。
 */
void MTaskEventLoop::RunOnce(int /* timeoutMs */) {
    OwnerThreadId.store(std::this_thread::get_id(), std::memory_order_release);

    TDeque<TTask> ToRun;
    {
        // 只在此处短暂持锁:摘链本身 O(1),避免任务体执行期间长期占锁。
        std::lock_guard<std::mutex> Lock(TaskMutex);
        ToRun.swap(PendingTasks);
    }
    for (TTask& T : ToRun) {
        if (T) {
            T();
        }
    }
}
