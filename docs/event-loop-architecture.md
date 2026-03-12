# 主/子事件循环架构

> 一个主 EventLoop（MEventLoop）容纳若干子 EventLoop，每帧按注册顺序依次执行。

## 1. 结构

```
MEventLoop (主循环)
├── AddStep(step, timeoutMs)   // 注册子循环，不持有所有权
├── RunOnce()                  // 依次调用各 step->RunOnce(timeoutMs)
└── 子循环（IEventLoopStep）
    ├── MTaskEventLoop   (timeout 通常 0，仅执行任务队列)
    ├── MNetEventLoop    (timeout 通常 16，poll + 收包/断线)
    └── 可扩展更多…
```

- **主循环**：`MEventLoop`（`Core/MEventLoop.h`）。只做两件事：维护子循环列表、每帧按序调用各子的 `RunOnce(timeoutMs)`。
- **子循环接口**：`IEventLoopStep`（`Core/IEventLoopStep.h`），仅 `RunOnce(int timeoutMs)`。子类自行解释 `timeoutMs`（如网络 poll 超时、任务循环可忽略）。
- **当前子循环**：
  - `MTaskEventLoop`：任务队列，实现 `ITaskRunner` + `IEventLoopStep`；通常注册为第一步（timeout 0）。
  - `MNetEventLoop`：监听 + 连接 poll + 可读时收包/断线；通常注册为第二步（timeout 16）。

## 2. 服务器侧用法

- `MNetServerBase` 持有：`MEventLoop MasterLoop`、`MTaskEventLoop TaskLoop`、`MNetEventLoop EventLoop`。
- 在 `Run()` 中首次进入时注册：`MasterLoop.AddStep(&TaskLoop, 0); MasterLoop.AddStep(&EventLoop, 16);`。
- 主循环：`while (bRunning) { MasterLoop.RunOnce(); TickBackends(); }`。
- 异步投递（Yield、MSequence）使用 `GetTaskRunner()`（即 `&TaskLoop`），与主/子结构解耦。

## 3. 扩展子循环

1. 实现 `IEventLoopStep`（提供 `RunOnce(int timeoutMs)`）。
2. 在驱动主循环的代码里（如 `MNetServerBase::Run()` 或子类）持有该子循环实例。
3. 在进入主循环前调用 `MasterLoop.AddStep(&YourLoop, timeoutMs)`，顺序和超时按需设定。

主循环不持有子循环所有权，生命周期由外部管理。
