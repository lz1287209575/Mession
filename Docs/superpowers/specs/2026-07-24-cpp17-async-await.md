# C++17 异步模型（SFutureResult / Async / AWAIT / MAsyncContext）— 设计 Spec

> 起草：2026-07-24  
> 状态：v1（讨论定稿，待实施）  
> 关联：  
> - `CLAUDE.md` / `TODO.md` / `Docs/RefactorArchitectureAndRpc.md`（PoC 拓扑与 backlog）  
> - `Source/Common/Runtime/Async/MAsync.h`（`SFutureResult`）  
> - `Source/Common/Runtime/Concurrency/Promise.h`（`MPromise` / `MFuture`）  
> - `Source/Common/Runtime/Concurrency/{FiberScheduler,FiberAwait}.*`（legacy 有栈路径）  
> - `Source/Servers/App/ServerCallAsyncSupport.h`  
> - `Source/Tools/MHeaderTool`（`MFUNCTION` / 将来状态机生成）

---

## 0. 一句话

在 **C++17**（无语言级 `co_await`）下，以已有 **`SFutureResult<T>`** 为唯一异步合同（≈ C# `Task<T>`），用 **`MFUNCTION(..., Async)`** 标记异步方法（≈ `async`），用 **宏 + MHeaderTool 生成状态机** 实现 **`AWAIT` / `AWAIT_OK`**（≈ `await`），用 **`Get`/`Wait`** 作为显式同步屏障（≈ `Task.Result`），用薄 **`MAsyncContext`** 决定 continuation 贴回哪条执行线（≈ `SynchronizationContext` 子集）。**有栈 Fiber / `MAwait` 退出主叙事。**

---

## 1. 目标

1. **C# 向体感**：业务可写「线性等待远端 RPC 再继续」的路径，而不是多层手写 `Then`。  
2. **C++17 可实现**：不依赖 C++20 coroutine；实现手段限定为 **Future 合同 + 状态机（宏/MHeaderTool）+ 事件循环 Post**。  
3. **合同单一**：异步返回类型统一为 **`SFutureResult<T>`**；废弃 **`MFUTURE(T)`** 宏包装。  
4. **标记清晰**：`MFUNCTION` 参数中的 **`Async`** 表示本方法允许 `AWAIT`，且返回的 future **可以未完成**。  
5. **传染可理解**：  
   - **硬规则**：`AWAIT` 仅允许出现在 `Async` 方法内；  
   - **非强制整链 Async**：非 Async 可用 `Then` 或（在允许阻塞的上下文）`Get`/`Wait` 消费异步 API，从而终结向上的 await 传染（对齐 C# `.Result`）。  
6. **调度可叙述**：引入 **`MAsyncContext`**，规定 resume 默认贴回进程事件循环（后续可扩展 Actor 串行队列）。  
7. **与 RPC 共存**：同步短 handler（返回已 ready 的 `SFutureResult`）继续合法；Async handler 要求 **dispatch 支持未完成 future 再回包**。  
8. **标准口径**：工程语言标准目标为 **C++17**（见 §12 与现状 CMake 差异）。

---

## 2. 非目标

1. **C++20 `co_await` / `co_return` / `std::coroutine_traits`** 作为主路径（本 spec 明确不用）。  
2. **全站有栈 fiber 作为默认执行模型**（`MFiberScheduler` 保留作 player-command 基础设施；`MAwait` / `MAwaitOk` 已于 P4 删除，见 `2026-07-28-async-p4-wrap.md` §C）。  
3. **完整复刻 .NET `SynchronizationContext` 生态**（无 OperationStarted 全家桶、无第三方上下文插件市场）。  
4. **跨进程异步上下文**（仅进程内）。  
5. **任意控制流的完整 C# 编译器**（v1 状态机语言子集见 §7.3）。  
6. **在本 spec 内实现业务 Manifest emit / validate 修绿**（并行 backlog，见 `TODO.md`）。  
7. **一次性迁移全仓库所有 handler 为 Async**（允许长期双模式：同步完成 vs Async）。

---

## 3. 现状基线

| 类别 | 状态 |
|------|------|
| **语言/构建** | `CMakeLists.txt` 当前为 `CMAKE_CXX_STANDARD 20`；本 spec 目标改为 **17**（实施 PR 改 CMake + 清理 co_* 注释） |
| **合同** | `SFutureResult<T>` 已存在（`MAsync.h`）；`#define MFUTURE(T) SFutureResult<T>` 广泛使用 |
| **Promise** | `MPromise`/`MFuture`：互斥、条件变量、`Then` 回调；`SetValue` **在完成线程同步调用 Then** |
| **RPC 返回** | 多数 `MFUNCTION(ServerCall)` 返回 `MFUTURE`，经 `MakeSuccessFuture` **立即完成** |
| **Fiber** | Linux `ucontext` 有栈；Windows null backend；`MAwait` 绑旧 `MPlayerCommand*`；业务几乎无 `CreateExecution` |
| **调度碎片** | `ITaskRunner::PostTask`、`MAsync::Yield`、Fiber resume 优先 Post；无统一 Ambient Context |
| **文档误导** | `MAsync.h` / `MEventAwait.h` 含 `co_await`/`co_return` 示意，与 C++17 目标冲突 |
| **Tool** | `MHeaderTool` 已解析 `MFUNCTION` / `MFUTURE(` 前缀；**尚无** Async 状态机生成 |

---

## 4. C# 对照表（心智模型）

| C# | Mession（本 spec） | 备注 |
|----|-------------------|------|
| `Task<T>` | `SFutureResult<T>` | 唯一业务异步合同 |
| `async` | `MFUNCTION(..., Async)`（类成员 + 自由函数统一入口，§6.2） | 标记「可 AWAIT、可未完成」 |
| `await expr` | `AWAIT(expr)` / `AWAIT_OK(expr)` | 宏 + 状态机，**非** fiber |
| `Task.FromResult` / 已完成 Task | `MakeSuccessFuture` / 已 Ready 的 future | 同步 handler 主路径 |
| `t.Result` / `GetAwaiter().GetResult()` | `Get()` / 阻塞 Wait 语义 | **同步屏障**；有线程红线 |
| `ContinueWith` | `Then` | 非 Async 也可 |
| `SynchronizationContext` | **`MAsyncContext`（薄）** | v1 主要是 Post 回 Loop |
| `ConfigureAwait(false)` | v1 不暴露；默认策略写死在 Context | 避免 API 爆炸 |

---

## 5. 核心类型与 API 约定

### 5.1 `SFutureResult<T>`（合同）

- 定义位置：`Source/Common/Runtime/Async/MAsync.h`（保持/打磨，不改名）。  
- 语义：  
  - 底层：`MFuture<TResult<T, FAppError>>`。  
  - `GetResult()` / `IsOk()` / `IsErr()`：不抛，给调用方判断。  
  - `Get()`：Err 时抛 `FFutureResultError`（给 await 恢复路径或显式同步屏障用）。  
- **禁止**再引入第二套「业务异步返回类型」宏包装。  
- **`MFUTURE(T)`**：  
  - 规范层面 **废弃**；  
  - 迁移期 MHeaderTool 可继续识别并告警；  
  - 新代码直接写 `SFutureResult<T>`。

### 5.2 成功/失败工厂

继续使用（或等价迁移到返回 `SFutureResult`）：

- `MServerCallAsyncSupport::MakeSuccessFuture(T)`  
- `MServerCallAsyncSupport::MakeErrorFuture<T>(Code, Message)`  
- `MakeResultFuture(TResult<...>)`

### 5.3 三种合法消费方式

```text
(1) AWAIT / AWAIT_OK     — 仅 Async 函数内；不阻塞事件循环（状态机挂起）
(2) Then(callback)       — 任意位置；回调风格；不要求标 Async
(3) Get() / Wait         — 同步屏障；终结 await 传染；见 §8 红线
```

---

## 6. 标记：`Async` 与传染规则

### 6.1 `MFUNCTION(..., Async)`

- **含义**：本方法是异步方法。  
  - 允许函数体出现 `AWAIT` / `AWAIT_OK`；  
  - 返回类型必须是 `SFutureResult<T>`；  
  - 返回的 future **允许未 Ready**（运行时 dispatch 必须处理）。  
- **可与其它 transport 组合**：如 `MFUNCTION(ServerCall, Async)`、`MFUNCTION(Async, CallClient)`（具体 tag 集合以 MHeaderTool 解析为准，本 spec 只要求 **Async 位存在**）。  
- **无 `Async` 且无 `AWAIT`**：允许返回 `SFutureResult<T>`，但约定 **返回时已 Ready**（同步短路径，PoC 默认）。  
- **无 `Async` 却出现 `AWAIT`**：MHeaderTool **必须报错**。

### 6.2 内部异步函数（非 RPC 入口）

RPC 入口用 `MFUNCTION`；工具函数（自由函数、namespace-scope helper）需要 await 时，使用 **`MFUNCTION(Async)`** 标记——与类成员路径走完全相同的 MHeaderTool 状态机生成（spec §7.2；P4 引入，见 `2026-07-28-async-p4-wrap.md` §B）。

```cpp
// 自由函数示例（P4 之后）—— 与类成员 MFUNCTION(..., Async) 等价
MFUNCTION(Async)
SFutureResult<FFoo> LoadFooAsync(int Seed);
```

- **不**做「仅因返回 `SFutureResult` 就自动当 async」的静默推断（避免误生成）。
- 自由函数上 `MFUNCTION(Async)` 不允许带 `ServerCall` / `ClientCall` / `RPC` 等 transport tag——MHeaderTool 会在该误用上报错并引用 `2026-07-28` spec。
- 若实现阶段证明宏噪音过大，可开附录变体「返回类型 + 含 AWAIT 才生成」；默认仍是显式标记。
- P0–P3 期间曾考虑过的 `MASYNC` 宏已由 P4 决定**不引入**（`MFUNCTION(Async)` 同时覆盖类成员与自由函数）。

### 6.3 传染性（硬 + 软）

**硬（工具强制）**

1. `AWAIT`/`AWAIT_OK` 只能出现在 `MFUNCTION(..., Async)` 函数中（类成员 + 自由函数均适用）。  
2. 被 `AWAIT` 的表达式类型必须是 `SFutureResult<T>`（或本 spec 明确列出的别名，v1 仅此一种）。  
3. `MFUNCTION(..., Async)` 函数的返回类型必须是 `SFutureResult<T>`。

**软（设计允许，对齐 `Task.Result`）**

4. 非 Async 函数 **可以调用** Async 函数，只要：  
   - **不**使用 `AWAIT`；且  
   - 使用 `Then`，或在 **§8 允许阻塞的上下文** 中使用 `Get`/`Wait`。  
5. 因此 **不要求**「从入口到叶子所有函数都标 Async」——仅 **线性 await 链** 上的函数需要 Async。

```text
Async A  --AWAIT--> Async B  --AWAIT--> Async C     OK
Sync  X  --Get()---> Async A                        OK（屏障，有红线）
Sync  X  --Then----> ...                            OK
Sync  X  --AWAIT--> ...                             ILLEGAL
```

---

## 7. `AWAIT` / 状态机（C++17 实现）

### 7.1 宏语义（业务源码层）

| 宏 | 语义 |
|----|------|
| `AWAIT(expr)` | 等待 `SFutureResult<T>`；恢复后得到 `TResult` 或按实施选择「拆成 Ok/Err 分支」；v1 推荐与 `AWAIT_OK` 分工见下 |
| `AWAIT_OK(expr)` | 等待完成；**Ok → 产出 `T`**；**Err → 完成外层 future 为同一错误并结束状态机**（不继续后续语句） |

v1 默认：**业务优先 `AWAIT_OK`**；需要区分错误时用 `AWAIT` + 显式分支（若 v1 子集暂不支持复杂分支，可第二阶段再加）。

宏 **不是** 真挂起原生栈；由 MHeaderTool 改写/生成状态机实现。

### 7.2 MHeaderTool 职责

对每个 `MFUNCTION(..., Async)` 且需要生成的函数：

1. 解析函数体中的 `AWAIT`/`AWAIT_OK` 锚点。  
2. 生成 `F{Func}_AsyncFrame`（或匿名命名空间内结构）：  
   - 捕获：`this`/参数/跨 await 存活的局部量；  
   - **`TSharedPtr`/`raw` 持有的 `MAsyncContext`**（§9）；  
   - `MPromise`/外层 `SFutureResult` 出口；  
   - `State` 整型。  
3. 公开入口：分配 Frame → `State=0` → `Resume()` → **立即返回** 可能未完成的 `SFutureResult`。  
4. `Resume()`：`switch(State)` 推进；遇未完成 await 则 `Then` + `Context.Post(Resume)` 后 return。  
5. 终态：`Promise.SetValue(Ok/Err)`。

### 7.3 v1 语言子集（强制收窄）

**允许**

- 顺序语句；  
- 简单 `if` / `else`（条件不跨未完成 await 的复杂依赖可后续放宽）；  
- **语句级** `AWAIT`/`AWAIT_OK`（单独语句或简单初始化）；  
- 有限 await 次数（实施可设上限，如 8，超限 Tool 报错）。

**禁止（v1）**

- `for`/`while` 循环体内 await；  
- 复杂表达式内嵌套多个 await；  
- 跨 await 的 `goto`；  
- 完整 try/catch 跨 await（可后续专项设计）；  
- 在 Async 函数内调用 **阻塞** `Get()` 等待「需本 Context 推进」的 future。

**垂直切片验收（P2）**：`Echo`（或等价）内 **一次** `AWAIT_OK(CallToActor(...))`，端到端回包，事件循环不因 await 卡死。

### 7.4 与 Fiber 的隔离

| | `AWAIT`（本 spec） | Fiber path（`MFiberScheduler`） |
|--|-------------------|--------------------------------|
| 模型 | 无栈状态机语义 | 有栈 ucontext fiber |
| 入口 | Async 状态机 Frame | `MFiberScheduler::CreateExecution` |
| 主路径 | **是** | **否（仅 player-command 基础设施）** |
| Windows | 与 Linux 同一套状态机 | null backend 不可 suspend |

P4 收口后：`MAwait` / `MAwaitOk` / `TPlayerCommandFuture` 已删除（`2026-07-28-async-p4-wrap.md` §C）。`FiberAwait.h` 仅保留 player-command runtime 钩子（`MHasCurrentPlayerCommand` / `MCurrentPlayerCommand` / `MCheckPoint` / `MYield` / `MPlayerCommandDetail::SuspendCurrentCommandUntil`）。业务层 handler 一律走本 spec 状态机路径。

---

## 8. 同步屏障（`Get` / `Wait` ≈ `Task.Result`）

### 8.1 合法用途

- 测试、工具、明确的同步适配层；  
- 不在「会推进该 future 的同一事件循环线程」上阻塞；  
- 用于 **终结** 向上的 await 传染，而不是业务中段的默认写法。

### 8.2 红线（必须写进实现与 code review）

1. **NetEventLoop / 处理当前入站包的线程**上，禁止对 **尚未 Ready、且其完成依赖于本循环继续跑** 的 `SFutureResult`/`MFuture` 调用阻塞 `Get`/`Wait`。  
2. 违规在 DEBUG 应 **assert 或 LOG_FATAL**（实施可选严格度）；RELEASE 至少 LOG_ERROR。  
3. Async 状态机内部等待 **只** 用 `AWAIT`，不用 `Get`。

### 8.3 死锁模式（文档必录）

```text
Loop 线程 → Handler → Get(等 RPC) → RPC 完成回调需 Loop 处理 → 永不完成
```

`MAsyncContext`（§9）用于辅助「当前是否在危险 Context」的判断。

---

## 9. `MAsyncContext`（薄 SynchronizationContext）

### 9.1 动机

`AWAIT` 恢复时必须回答：**continuation 在哪条执行线上跑？**  
否则 `Then` 在完成线程同步执行业务，会破坏串行假设或与事件循环死锁规则纠缠不清。

### 9.2 v1 职责（仅此）

```cpp
// 示意 API（最终命名/头文件路径实施 PR 定）
class MAsyncContext : public TSharedFromThis<MAsyncContext>  // 或非 shared，实施定
{
public:
    virtual ~MAsyncContext() = default;

    // 将续体投递到本上下文所属执行线（≈ SynchronizationContext.Post）
    virtual void Post(TFunction<void()> Continuation) = 0;

    // 可选：是否已在本上下文执行线（允许 inline 跑以减延迟）
    virtual bool IsSameContext() const { return false; }
};
```

**v1 不做**：OperationStarted/Completed、任意嵌套劫持、跨模块第三方上下文注册中心。

### 9.3 v1 实例

| 类型 | 绑定 | 用途 |
|------|------|------|
| **`MLoopAsyncContext`** | 进程 `ITaskRunner` / `MNetEventLoop` | Gateway/Echo/Registry **默认** resume 点 |

中期（非本 spec 必达）：`MActorStrandContext`（每 Actor 串行队列）。

### 9.4 捕获与使用

1. **请求入口**（accept 包、开始跑 handler / 创建 Async Frame）时，将当前进程默认 Loop Context 写入：  
   - TLS ambient（可选，便于库代码查询）；  
   - **Async Frame 字段（必须）**——resume 不依赖 TLS 仍在原线程。  
2. `AWAIT` 生成代码：

```text
fut.Then([Frame](auto done) {
    Frame->Context->Post([Frame, done]{ Frame->Apply(done); Frame->Resume(); });
});
```

3. 若 `IsSameContext()==true`，策略二选一（实施写死一种，推荐先 **始终 Post** 换简单正确；热路径再优化 inline）。

### 9.5 与 `MPromise::SetValue` 同步 Then 的关系

- 底层可继续同步触发「调度用」短回调。  
- **业务 Resume 与用户可见 Then 包装** 应经 `MAsyncContext::Post`，避免在任意完成线程直接跑业务。  
- 具体是「改造全局 Then」还是「仅状态机路径 Post」：实施可选；**状态机路径必须 Post** 为硬要求。

### 9.6 Ambient 查询（给 Get 红线用）

```text
MAsyncContext* MAsyncContext::Current();  // TLS，可空
```

- 在 Loop 线程处理包时 `Current() == LoopContext`。  
- `Get()` 若检测到「当前 Context 可能自依赖」→ 触发 §8.2。

---

## 10. RPC / 事件循环集成

### 10.1 Dispatch 契约

调用 `MFUNCTION(ServerCall[, Async])` 实现后：

```text
SFutureResult<T> F = Invoke(...);
if (F 已 Ready)
    立即写 FunctionResponse
else
    F.Then → Context.Post → 写 FunctionResponse
当前栈返回；不阻塞等 F
```

- **无 Async、同步完成**：与今日 PoC 行为兼容。  
- **Async 未完成**：必须走 Then 路径；这是本 spec 对运行时的 **硬要求**。

### 10.2 进程默认 Context 安装点

建议在 `MServiceMain::Run` 或各 `MNetServerBase::Run` 进入事件循环前：

- 创建 `MLoopAsyncContext` 绑到该进程 EventLoop/TaskRunner；  
- 设为 ambient 默认。

---

## 11. 目标架构总图

```text
                    ┌──────────────────────────┐
                    │  MFUNCTION(Async)        │
                    │  + Async 标记            │
                    └────────────┬─────────────┘
                                 │ 源码 + AWAIT 锚点
                    ┌────────────▼─────────────┐
                    │  MHeaderTool 状态机生成   │
                    │  Frame + Resume + Context │
                    └────────────┬─────────────┘
                                 │ 返回 SFutureResult（可 pending）
          ┌──────────────────────┼──────────────────────┐
          ▼                      ▼                      ▼
   已 Ready 立即回包      Then+Post Resume         同步边界 Get
          │                      │                 （§8 红线）
          └──────────┬───────────┘
                     ▼
            MAsyncContext::Post
                     │
                     ▼
            NetEventLoop / ITaskRunner
```

---

## 12. 语言标准与仓库卫生

| 项 | 动作 |
|----|------|
| 目标标准 | **C++17** |
| CMake | 实施 PR：`CMAKE_CXX_STANDARD 17`，bootstrap/`-std=` 同步 |
| 注释 | 删除/改写 `co_await`/`co_return` 作为「现行写法」的注释 |
| `MFUTURE` | 废弃迁移；Tool 过渡识别 |
| Fiber 文档 | 标 legacy；不写进新业务指南 |

若短期内 CMake 仍保持 20 **仅因工具链**，仍 **禁止** 使用 C++20 coroutine 语法作为业务 async 手段；本 spec 的实现不得依赖 coroutine。

---

## 13. 文件与模块规划（实施参考）

| 路径 | 动作 |
|------|------|
| `Source/Common/Runtime/Async/MAsync.h` | 强化 `SFutureResult` 文档；deprecate `MFUTURE` |
| `Source/Common/Runtime/Async/AsyncContext.h`（新） | `MAsyncContext` / `MLoopAsyncContext` |
| `Source/Common/Runtime/Async/AwaitMacros.h`（新） | `AWAIT`/`AWAIT_OK` 宏 + `MFUNCTION(Async)` 自由函数支持（与生成约定配合） |
| `Source/Common/Runtime/Concurrency/Promise.h` | 按需：Then 与 Post 协作（最小改） |
| `Source/Common/Runtime/Concurrency/Fiber*` | 不扩展；注释 deprecated |
| `Source/Tools/MHeaderTool/**` | Async 扫描、状态机生成、错误诊断 |
| `Source/Servers/**` 与 `MRpcChannel` dispatch | 未完成 future 回包路径 |
| `Docs/CodingStyle.md` / `CLAUDE.md` | 实施后补异步快查（可另 PR） |

---

## 14. 分阶段实施与验收

| 阶段 | 内容 | 验收 |
|------|------|------|
| **P0 口径** | 本 spec 合入；CMake/注释标准口径；`MFUTURE` 废弃声明 | 文档与构建标准一致；无新代码使用 co_* |
| **P1 运行时** | `MLoopAsyncContext`；dispatch 支持 pending `SFutureResult`；Get 红线 assert/日志 | 单测或最小 demo：pending future 能回包；loop 线程危险 Get 可检测 |
| **P2 垂直切片** | 一个 `ServerCall, Async` + 单点 `AWAIT_OK(CallToActor)`（可先手写 Frame，Tool 后补） | 跨 Echo 或本机二次 RPC 线性写法跑通；loop 不因 await 卡死 |
| **P3 Tool** | MHeaderTool 生成状态机；迁移示例 handler 去 `MFUTURE` | 生成代码编译通过；与手写切片行为一致 |
| **P4 收口** | `MFUNCTION(Async)` 扩展到自由函数（不再引入 `MASYNC`）；删除 `MAwait` / `MAwaitOk` / `TPlayerCommandFuture`；父 spec 同步升级；`CLAUDE.md` 加 async 快查表 | 新异步代码只走本 spec 路径；grep `MAwait` / `MASYNC` / `TPlayerCommandFuture` 仅命中已废弃的基础设施注释 |

---

## 15. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 状态机子集过窄，业务写不出 | v1 只保垂直切片；用 Then 做逃逸舱 |
| Tool 生成 bug 难调 | Frame 可 LOG State；保留手写 Frame 参考实现 |
| `SetValue` 同步 Then 踩线程 | 状态机路径强制 `Context.Post` |
| 误用 `Get` 死锁 | §8 + Context 检测 + code review |
| 双模型（Fiber + AWAIT）混乱 | P4 收口：`MAwait` / `MAwaitOk` / `TPlayerCommandFuture` 已删除；`FiberAwait.h` 仅保留 player-command 基础设施；业务一律走状态机路径 |
| CMake 20 vs 目标 17 | P0 统一；CI 用 17 编译 |

---

## 16. 决策记录（已确认）

1. 语言目标 **C++17**，主路径 **不用** `co_await`。  
2. 合同类型 **`SFutureResult<T>`**，**废弃 `MFUTURE` 包装**。  
3. **`MFUNCTION(..., Async)`** = async 标记（统一覆盖类成员 + namespace-scope 自由函数；不再引入 `MASYNC`）。  
4. **`AWAIT` 仅 Async 内**（硬传染）；**非**要求全调用栈所有函数皆 Async。  
5. **`Get`/`Wait` ≈ `Task.Result`**，可在同步边界终结传染，受事件循环红线约束。  
6. 实现 = **宏 + MHeaderTool 状态机 + Then + Post**。  
7. **`MAsyncContext`** = 薄 SyncContext，v1 贴 Loop。  
8. **Fiber** = 仅作 player-command 基础设施保留；`MAwait` / `MAwaitOk` / `TPlayerCommandFuture` 已于 P4 删除（`2026-07-28-async-p4-wrap.md` §C），不再是非主模型——已**移除**。

---

## 17. 开放问题（实施前可再拍板）

| ID | 问题 | 默认倾向 |
|----|------|----------|
| Q1 | pending 完成时是否 **永远** `Post`，还是 `IsSameContext` 时 inline？ | v1 **永远 Post** |
| Q2 | `AWAIT`（非 OK）是否 v1 就做？ | v1 可先只做 **`AWAIT_OK`** |
| Q3 | 全局改造 `Then` 自动 Post，还是仅状态机 Post？ | **仅状态机硬要求 Post** |
| Q5 | CMake 改 17 与本功能 PR 合并还是拆开？ | **P0 可单独 PR 改标准 + 文档** |

---

## 18. 附录 A：作者速查

```cpp
// 同步短路径（合法）
MFUNCTION(ServerCall)
SFutureResult<FResp> Foo(const FReq& R)
{
    return MakeSuccessFuture(FResp{});
}

// 异步线性（幸福路径）
MFUNCTION(ServerCall, Async)
SFutureResult<FResp> Bar(const FReq& R)
{
    FResp Remote = AWAIT_OK(CallToActor(...));
    return MakeSuccessFuture(std::move(Remote));
}

// 同步边界（类 .Result；勿在 net loop 上对自依赖 future 使用）
void Tool()
{
    SFutureResult<FResp> F = Bar(Req);
    FResp V = F.Get();
}

// 回调（不标 Async）
Bar(Req).Then([](SFutureResult<FResp> F) { /* ... */ });
```

## 附录 B：术语

- **合同**：类型层异步结果（`SFutureResult`）。  
- **状态机 / Frame**：无栈语义的挂起恢复载体。  
- **同步屏障**：`Get`/`Wait`。  
- **上下文 / MAsyncContext**：continuation 投递目标。  
- **硬传染**：AWAIT→必须 Async。  
- **软边界**：Get/Then 可不标 Async。

---

## 附录 C：修订历史

| 日期 | 说明 |
|------|------|
| 2026-07-24 | v1：C++17、SFutureResult、Async/AWAIT、Get 屏障、MAsyncContext、Fiber legacy、分期验收 |
| 2026-07-28 | v2：P4 收口后修订——`MASYNC` 不引入；`MAwait` / `MAwaitOk` / `TPlayerCommandFuture` 删除；§6.2 / §7.4 / §14 / §16.8 / §17 Q4 同步（见 `2026-07-28-async-p4-wrap.md`） |
