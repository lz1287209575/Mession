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
| `MNetEventLoop::PostTask` | Core/EventLoop | 将 `TFunction<void()>` 投递到下一轮 RunOnce 执行 |
| `MAsync::Yield(loop, next)` | Core/Async.h | 把 `next` 投递到下一 tick 执行（即 PostTask 的语义化封装） |
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

### 3.2 EventLoop PostTask

```cpp
loop.PostTask([]() { /* 下一 RunOnce 执行 */ });
```

- 线程安全（内部队列 + 互斥）；RunOnce 在本线程消费任务。

### 3.3 协程风格（C++17，无语言协程）

**方式 A：Yield(loop, next)**  
“当前逻辑跑完，下一 tick 再跑 next”：

```cpp
void Step1(MNetEventLoop* Loop) {
    DoWork();
    MAsync::Yield(Loop, [] { Step2(); });  // 下一 tick 执行 Step2
}
```

**方式 B：Sequence —— 多步序列，步间自动“让出”到下一 tick**

```cpp
auto Seq = MAsync::Sequence::Create(Loop);
Seq->Do([]() { DoWork(); });       // 第 1 步
Seq->Do([]() { MoreWork(); });    // 第 2 步（上一步返回后自动投递到下一 tick）
Seq->Do([]() { Finish(); });      // 第 3 步
Seq->Run();                        // 执行第 1 步，后续步在后续 RunOnce 中依次执行
```

- 不依赖 C++20；仅用 `TFunction` + PostTask 实现。
- “协程”语义由库提供：分段执行、步间让出事件循环。

## 4. 实现阶段

| 阶段 | 内容 | 验收 |
|------|------|------|
| 1 | `MPromise<T>` / `MFuture<T>`，单线程或简单互斥 | 单测或脚本验证 Get/SetValue/Then |
| 2 | `MNetEventLoop::PostTask`，RunOnce 中执行队列任务 | 单测：Post 后 RunOnce 一次即执行 |
| 3 | `MAsync::Yield(loop, next)`（封装 PostTask） | 单测：Yield 后下一 tick 执行 next |
| 4 | `MAsync::Sequence`：Do / Run，步间自动 PostTask 下一步 | 单测：多步顺序、步间跨 tick |

## 5. 与现有代码的关系

- **EventLoop**：仍为单线程；PostTask 在本线程 RunOnce 中执行。
- **服务器**：可在回调或 TickBackends 里 `PostTask` 或 `Sequence::Do` 做延后/分步逻辑。
- **STL 封装**：不直接暴露 `std::future`/`std::promise`，统一用 `MPromise`/`MFuture`。

## 6. 风险与注意

- **生命周期**：Yield/Sequence 里捕获的 lambda 或指针需在任务执行时仍有效；避免在已析构的 EventLoop 上投递任务。
- **异常**：Promise 侧 `SetException`、Future 侧 `Get()` 可重新抛出；PostTask/Sequence 内异常需在任务内部捕获或统一处理，避免抛到 RunOnce 外。

## 7. 为何不用 C++20 协程

- 项目可能降级到 **C++17**，语言级 `co_await`/`co_return`/`promise_type` 不可用。
- 采用库级“步进 + Yield/Sequence”可在 C++11/14/17 下统一使用，行为清晰、易维护。
