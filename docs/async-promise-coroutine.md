# Async / Promise / Coroutine 机制

> 项目级异步与协程设施，**不依赖 C++20 语言协程**，兼容 C++17。遵循 STL 封装规则（M* / S*），与 EventLoop 单线程模型配合。

## 1. 目标与范围

- **Async**：在事件循环上延后执行任务（下一 tick 或指定时机），避免阻塞 poll。
- **Promise/Future**：单消费者、可跨调用链传递的“未来结果”，支持 `Then` 续延（可选）。
- **Coroutine**：**库级“协程风格”**，通过步进序列或 Yield 回调在 EventLoop 上分步执行，不依赖 `co_await`/`co_return`，降级到 C++17 仍可用。

约束：零外部依赖；先单线程（EventLoop 线程）使用；C++17 兼容。

## 2. 组件概览

| 组件 | 位置 | 说明 |
|------|------|------|
| `MPromise<T>` / `MFuture<T>` | Core/Promise.h | 一对多不可、可移动；SetValue/SetException；Get/Wait/Then |
| `ITaskRunner` / `PostTask` | Core/ITaskRunner.h | 抽象“可投递任务、下一 tick 执行”；`MTaskEventLoop` 实现（Core/TaskEventLoop.h） |
| `MAsync::Yield(runner, next)` | Core/Async.h | 把 `next` 投递到 runner 下一 tick（仅依赖 `ITaskRunner`，不依赖网络） |
| `MAsync::MSequence` | Core/Async.h | 多步“协程风格”序列：Do(step1); Do(step2); Run()；步与步之间在下一 tick 自动衔接 |

## 3. API 草图

### 3.1 Promise / Future

```cpp
MPromise<int> P;
MFuture<int> F = P.GetFuture();

// 生产者
P.SetValue(42);
// 或 P.SetException(std::current_exception());

// 消费者
if (F.Valid()) {
    int x = F.Get();  // 阻塞直到有值或异常
}
F.Then([](MFuture<int> f) { /* 使用 f.Get() */ });
```

- `GetFuture()` 仅能调用一次；`SetValue`/`SetException` 仅能调用一次。
- `Get()` 在未就绪时阻塞；单线程场景可多用 `Then` + PostTask 避免阻塞。

### 3.2 ITaskRunner / MTaskEventLoop

```cpp
// 服务器内使用基类提供的任务循环（每帧先 TaskLoop.RunOnce() 再网络 poll）
ITaskRunner* runner = GetTaskRunner();  // MNetServerBase 提供
runner->PostTask([]() { /* 下一 tick 执行 */ });
```

- 异步（Yield/Sequence）只依赖 `ITaskRunner`；任务由 **MTaskEventLoop** 执行，与 **MNetEventLoop**（仅网络 poll）分离。
- 服务器主循环顺序：`TaskLoop.RunOnce()` → `EventLoop.RunOnce(16)` → `TickBackends()`。

### 3.3 协程风格（C++17，无语言协程）

**方式 A：Yield(runner, next)**  
“当前逻辑跑完，下一 tick 再跑 next”。`runner` 为任意 `ITaskRunner*`（如 `&EventLoop`）：

```cpp
void Step1(ITaskRunner* Runner) {
    DoWork();
    MAsync::Yield(Runner, [] { Step2(); });  // 下一 tick 执行 Step2
}
```

**方式 B：Sequence —— 多步序列，步间自动“让出”到下一 tick**

```cpp
auto Seq = MAsync::MSequence::Create(Runner);  // Runner 为 ITaskRunner*
Seq->Do([]() { DoWork(); });       // 第 1 步
Seq->Do([]() { MoreWork(); });     // 第 2 步（上一步返回后自动投递到下一 tick）
Seq->Do([]() { Finish(); });       // 第 3 步
Seq->Run();                        // 执行第 1 步，后续步在后续 RunOnce 中依次执行
```

- 不依赖 C++20；仅用 `TFunction` + PostTask 实现。
- “协程”语义由库提供：分段执行、步间让出事件循环。

## 4. 实现阶段

| 阶段 | 内容 | 验收 |
|------|------|------|
| 1 | `MPromise<T>` / `MFuture<T>`，单线程或简单互斥 | 单测或脚本验证 Get/SetValue/Then |
| 2 | `MTaskEventLoop` 实现 `ITaskRunner::PostTask`，RunOnce 仅执行队列任务 | 单测：Post 后 RunOnce 一次即执行 |
| 3 | `MAsync::Yield(loop, next)`（封装 PostTask） | 单测：Yield 后下一 tick 执行 next |
| 4 | `MAsync::MSequence`：Do / Run，步间自动 PostTask 下一步 | 单测：多步顺序、步间跨 tick |

## 5. 与现有代码的关系

- **MTaskEventLoop**：纯任务队列，RunOnce 只执行已投递任务；与 **MNetEventLoop**（仅网络 poll）分离，同线程顺序执行。
- **服务器**：基类提供 `GetTaskRunner()`（即 `&TaskLoop`）；在回调或 TickBackends 里用 `GetTaskRunner()->PostTask(...)` 或 `MSequence::Create(GetTaskRunner())` 做延后/分步逻辑。
- **STL 封装**：不直接暴露 `std::future`/`std::promise`，统一用 `MPromise`/`MFuture`。

## 6. 风险与注意

- **生命周期**：Yield/Sequence 里捕获的 lambda 或指针需在任务执行时仍有效；避免在已析构的 EventLoop 上投递任务。
- **异常**：Promise 侧 `SetException`、Future 侧 `Get()` 可重新抛出；PostTask/Sequence 内异常需在任务内部捕获或统一处理，避免抛到 RunOnce 外。

## 7. 协程怎么用（示例）

### 头文件

```cpp
#include "Core/EventLoop.h"
#include "Core/Async.h"
```

### 用法一：Yield —— “这一帧做完，下一帧再做下一段”

适合「先做 A，让出事件循环一帧，再做 B」：

```cpp
void OnClientConnected(ITaskRunner* Runner, uint64 ConnId) {
    DoInitialSetup(ConnId);
    MAsync::Yield(Runner, [Runner, ConnId]() {
        SendWelcomePacket(ConnId);
        MAsync::Yield(Runner, [ConnId]() {
            StartHeartbeat(ConnId);
        });
    });
}
```

每一层 `Yield(Runner, next)` 表示：当前逻辑结束 → 下一 tick 再执行 `next`。`Runner` 可为任意实现 `ITaskRunner` 的循环（服务器里通常传 `&EventLoop`）。需要跨步共享状态时用 lambda 捕获（如 `ConnId`、`Runner`）。

### 用法二：MSequence —— 多步流水，步与步之间自动隔一帧

适合「步骤 1 → 等一帧 → 步骤 2 → 等一帧 → 步骤 3」的线性流程：

```cpp
void StartLoginFlow(ITaskRunner* Runner, uint64 ConnId) {
    auto Seq = MAsync::MSequence::Create(Runner);
    Seq->Do([ConnId]() { ValidateClientVersion(ConnId); });
    Seq->Do([ConnId]() { LoadPlayerData(ConnId); });
    Seq->Do([ConnId]() { SendEnterWorld(ConnId); });
    Seq->Run();  // 第 1 步立刻执行，第 2、3 步在后续 RunOnce 中依次执行
}
```

`Run()` 会立刻执行第 1 步；第 1 步返回后，库内部会 `PostTask` 第 2 步，再下一 tick 执行第 3 步。每一步用无参 `void()` lambda，传参靠捕获。`Seq` 由 `TSharedPtr` 管理，未执行完的步骤会通过 PostTask 持有该 shared_ptr，只要 Loop 在跑，序列不会被提前析构。

### 在服务器里用（例如 Gateway OnAccept）

若在 `OnAccept` 里用，基类提供 `GetTaskRunner()`（即 `&TaskLoop`），传给它即可：

```cpp
void MGatewayServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) {
    Conn->SetNonBlocking(true);
    // ... 注册连接等 ...
    auto Seq = MAsync::MSequence::Create(GetTaskRunner());
    Seq->Do([this, ConnId]() { SendServerTime(ConnId); });
    Seq->Do([this, ConnId]() { MaybeQueueForWorld(ConnId); });
    Seq->Run();
}
```

### 注意点

- **单线程**：所有步骤都在 EventLoop 所在线程执行，某一步里不要长时间阻塞或重 CPU，否则会拖住 poll。
- **捕获安全**：Yield/Sequence 的 lambda 里捕获的指针、引用必须在“执行那一刻”仍有效；避免捕获已析构对象。
- **异常**：某一步抛异常会中断该步，后续步不会自动执行；需在步内 try/catch 或自行在 RunStep 层统一处理。

## 8. 为何不用 C++20 协程

- 项目可能降级到 **C++17**，语言级 `co_await`/`co_return`/`promise_type` 不可用。
- 采用库级“步进 + Yield/MSequence”可在 C++11/14/17 下统一使用，行为清晰、易维护。
