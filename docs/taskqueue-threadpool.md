# TaskQueue 与 ThreadPool 设计

> 项目级任务队列与线程池，用于多线程执行无返回值或带返回值的任务。C++17 兼容，零外部依赖，遵循 M* / S* 命名与 STL 封装规则。

## 1. 目标与范围

- **TaskQueue（MTaskQueue）**：线程安全的任务队列，多生产者多消费者（MPMC），任务类型为 `TFunction<void()>`。
- **ThreadPool（MThreadPool）**：固定数量工作线程，共享一个 TaskQueue，从队列取任务并执行；支持 `Submit(task)`，可选与 `MPromise`/`MFuture` 结合返回结果。

约束：零外部依赖；仅用标准库 `std::thread`、`std::mutex`、`std::condition_variable`、`std::queue`/`TQueue`；C++17 兼容。

## 2. 组件概览

| 组件 | 位置 | 说明 |
|------|------|------|
| `MTaskQueue` | Core/TaskQueue.h | 线程安全队列：Push、TryPop、Pop(阻塞/超时)、Size、Clear、Shutdown |
| `MThreadPool` | Core/ThreadPool.h | 固定 N 个工作线程 + 一个 MTaskQueue：Submit、Shutdown、WaitIdle |

## 3. MTaskQueue 设计

### 3.1 职责

- 存放待执行任务：`TFunction<void()>`。
- 多线程可安全地 **Push**（生产者）和 **Pop**（消费者）。
- 支持「关闭」语义：Shutdown 后 Push 不再接受新任务，Pop 在队列空时返回空（不再阻塞等待）。

### 3.2 接口草图

```cpp
class MTaskQueue
{
public:
    using TTask = TFunction<void()>;

    MTaskQueue();
    ~MTaskQueue();

    // 入队（Shutdown 后可选择性拒绝或忽略，建议拒绝并返回 false）
    bool Push(TTask Task);
    bool Push(TTask&& Task);

    // 出队：非阻塞，若队列空返回 false，OutTask 不变
    bool TryPop(TTask& OutTask);

    // 出队：阻塞直到有任务或 Shutdown；若带超时则超时后返回 false
    bool Pop(TTask& OutTask);
    bool Pop(TTask& OutTask, int TimeoutMs);

    // 当前队列中任务数（近似值亦可）
    size_t Size() const;

    // 清空未执行任务（可选，需考虑正在被 Pop 的并发）
    void Clear();

    // 关闭队列：此后 Push 返回 false，Pop 在空时立即返回 false
    void Shutdown();
    bool IsShutdown() const;
};
```

### 3.3 实现要点

- 内部：`TQueue<TTask>`（或 `TDeque`）+ `std::mutex` + `std::condition_variable`。
- **Push**：加锁，若已 Shutdown 返回 false；否则入队并 notify_one。
- **TryPop**：加锁，若空返回 false；否则出队并返回 true。
- **Pop**：加锁，循环 wait 直到非空或 Shutdown；若带超时用 `wait_for`。
- **Shutdown**：置位，notify_all，使阻塞在 Pop 的线程退出。
- **Clear**：加锁，清空队列（仅清队列，不中断正在执行的任务）。

### 3.4 可选扩展

- **最大容量**：Push 在队列满时阻塞或返回 false（可配置）。
- **优先级**：若后续需要，可改为优先队列或双队列（高/普），接口仍以 Push/TryPop/Pop 为主。

## 4. MThreadPool 设计

### 4.1 职责

- 持有 **一个** `MTaskQueue` 和 **固定数量** 的 worker 线程。
- 每个 worker 循环：从队列 **Pop**，若取到任务则执行，若队列已 Shutdown 且空则退出循环。
- 对外提供 **Submit(task)**，内部 Push 到队列；可选 **Submit** 重载返回 `MFuture<T>`（需与 Promise 机制结合）。

### 4.2 接口草图

```cpp
class MThreadPool
{
public:
    using TTask = MTaskQueue::TTask;

    explicit MThreadPool(size_t NumThreads);
    ~MThreadPool();

    // 提交无返回值任务
    bool Submit(TTask Task);
    bool Submit(TTask&& Task);

    // 提交带返回值任务（若已实现 MPromise/MFuture）
    // template<typename T>
    // MFuture<T> Submit(TFunction<T()> Task);

    // 关闭队列并等待所有 worker 退出
    void Shutdown();

    // 仅等待当前队列中任务被执行完（队列空），不关闭队列
    void WaitIdle();

    // 线程数量
    size_t GetNumThreads() const;
};
```

### 4.3 实现要点

- 构造：创建 `MTaskQueue`，创建 `NumThreads` 个 `std::thread`，每个线程运行：
  ```cpp
  while (true) {
      TTask t;
      if (!Queue.Pop(t)) break;  // Shutdown 且空
      if (t) t();
  }
  ```
- **Submit**：`Queue.Push(std::move(Task))`，返回 Push 结果。
- **Shutdown**：先 `Queue.Shutdown()`，再 join 所有 worker。
- **WaitIdle**：需要「队列空」信号：可在 MTaskQueue 增加 `WaitUntilEmpty(int TimeoutMs)`（Pop 侧在每次 Pop 成功后 notify；或由 ThreadPool 用条件变量 + 任务计数实现）。简单实现：ThreadPool 维护一个「已提交未执行」计数，Submit 时 +1，worker 执行完一次后 -1 并 notify；WaitIdle 则 wait 直到计数为 0。

### 4.4 与 Promise/Future 的配合（可选）

- 若已有 `MPromise<T>`/`MFuture<T>`：
  - `Submit(TFunction<T()> f)` 内部：创建 `MPromise<T>`，Push 任务 `[p, f]() { p.SetValue(f()); }`，返回 `p.GetFuture()`。
  - 调用方通过 `Future.Get()` 或 `Then` 取结果，注意 Get() 阻塞在调用线程，适合在非 worker 线程使用。

## 5. 与现有组件的关系

| 组件 | 关系 |
|------|------|
| EventLoop | 独立：EventLoop 单线程 poll；ThreadPool 多线程执行任务。若需「在 EventLoop 线程拿结果」，可用 Future.Then + EventLoop.PostTask。 |
| Promise/Future | ThreadPool 可产出 MFuture（Submit 带返回值重载）；TaskQueue 不直接依赖 Promise。 |
| 服务器 | 耗时或阻塞操作可 Submit 到 ThreadPool，完成后再通过 PostTask 回主循环处理结果。 |

## 6. 文件与依赖

- **Core/TaskQueue.h**（及可选 Core/TaskQueue.cpp）：MTaskQueue，依赖 NetCore.h（TFunction、TQueue/TDeque、mutex 等）。
- **Core/ThreadPool.h**（及可选 Core/ThreadPool.cpp）：MThreadPool，依赖 TaskQueue.h、NetCore.h、\<thread\>。
- 不依赖 Logger、EventLoop；若实现 Submit 返回 Future，依赖 Core/Promise.h。

## 7. 实现阶段建议

| 阶段 | 内容 | 验收 |
|------|------|------|
| 1 | MTaskQueue：Push/TryPop/Pop/Shutdown/Size，锁+条件变量 | 单测：多线程 Push、多线程 Pop、Shutdown 后 Pop 立即返回 |
| 2 | MThreadPool：固定 N 线程 + 一个 MTaskQueue，Submit/Shutdown/Join | 单测：Submit 若干任务，Shutdown，确认全部执行且线程退出 |
| 3（可选） | WaitIdle（等队列空） | 单测：Submit 后 WaitIdle 再 Shutdown |
| 4（可选） | Submit 返回 MFuture<T>（依赖 Promise 实现） | 单测：Submit 带返回值任务，Get() 得到结果 |

## 8. 风险与注意

- **异常**：任务执行中抛异常应限制在 worker 内捕获并记录（或存入 Promise.SetException），避免抛到 worker 循环外导致线程退出。
- **生命周期**：Submit 的 lambda 捕获需在任务执行时仍有效；避免捕获已析构对象。
- **Shutdown 顺序**：先 Shutdown 队列，再 join 线程，避免 Push 在 Shutdown 之后仍被调用导致不一致。
