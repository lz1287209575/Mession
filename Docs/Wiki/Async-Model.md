# C++17 异步模型（Async / Await）— 实现说明

> 本文档浓缩 Mession C++17 异步模型的**已实现能力**与当前用法。设计讨论、决策过程与
> P0–P5 历史分期见 `Docs/superpowers/specs/` 下的 async 系列 spec（文末"相关实现"），
> 这里不再重复。

## 0. 概览

在 **C++17**（无语言级 `co_await`）下，业务以 **`SFutureResult<T>`** 为唯一异步返回合同
（≈ C# `Task<T>`），用 **`MFUNCTION(..., Async)`** 标记异步方法（≈ `async`），在函数体内用
**`TAwaitable<F>(args...)`**（或 Frame 内 **`AWAIT_OK(expr)`**）实现挂起等待（≈ `await`），
用 **`Get()` / `Wait()`** 作为显式同步屏障（≈ `Task.Result`）。实现手段 = **Future 合同 +
MHeaderTool 生成状态机 + `MAsyncContext::Post` 贴回执行线**，非 fiber、非 C++20 coroutine。

有栈 Fiber（`MFiberScheduler`）仅保留为 player-command 基础设施；业务层异步一律走本模型
（`MAwait` / `MAwaitOk` / `TPlayerCommandFuture` / `MASYNC` / `MFUTURE` 均已删除）。

## 1. 核心类型

### 1.1 `SFutureResult<T>`（唯一异步合同）

定义：`Source/Common/Runtime/Async/MAsync.h`

- 是 `MFuture<TResult<T, FAppError>>` 的子类，完整保留 future 语义（`Then` / `IsReady` 等）。
- **是业务异步返回的唯一类型**——Async 函数必须返回 `SFutureResult<T>`；`InnerType = T`
  是 codegen / `Awaitable.h` 取内层类型的统一入口。
- 查询 / 取值（不抛）：
  - `GetResult()` → 原始 `TResult<T, FAppError>`；
  - `IsOk()` / `IsErr()` / `GetError()`；
  - `PeekResult()` → 非破坏性取已就绪的值（不 move 出，须 `IsReady()` 后再调）。
- `Get()`：Err 时抛 `FFutureResultError`（§1.2）；就绪前调用会触发死锁红线检测（§3.3）。
- 构造：从 `TResult<T, FAppError>` 直接构造（立即完成的 future）；从基类
  `MFuture<TResult<...>>` 隐式构造 / 转换。
- awaitable 协议：`AsAwaiter()` 返回 `SAwaiter`（`AwaitReady` / `AwaitSuspend` / `AwaitResume`
  三方法）——任何返回 `SFutureResult<T>` 的表达式都可被 codegen 状态机 await。

### 1.2 `FFutureResultError`（统一错误类型）

定义：`Source/Common/Runtime/Async/MAsync.h`

- 继承 `std::exception`，包装 `FAppError`；`what()` 返回 `"Code: Message"` 或纯 Code/Message；
  `GetError()` 取回原始 `FAppError`。
- 抛出场景：`SFutureResult::Get()` 遇到 Err；状态机 `AwaitResume()` 遇到 Err。

### 1.3 `MAsyncContext` / `MLoopAsyncContext`（薄 SynchronizationContext）

定义：`Source/Common/Runtime/Async/AsyncContext.h`

- 一个 context 代表一条"执行线"（进程事件循环 / 未来的 actor strand），决定 await 恢复时
  continuation 贴回哪条线。
- `Post(Continuation)` — 把续体投递到本 context 的执行线（可跨线程调用）。
- `IsSameContext()` — 当前线程是否就是本 context 的执行线（Get 红线检测用）。
- `Current()` / `SetCurrent()` — TLS ambient（请求入口 / 进程安装，业务代码不调 SetCurrent）。
- **`MLoopAsyncContext`**（默认实现）绑定 `mession::async::IExecutor`（进程事件循环），
  `IsSameContext()` 即 `Executor->IsCurrentThread()`。
- 状态机用法：await 未就绪时 `Then([Frame, Ctx]{ Ctx->Post([Frame, F]{ Frame->Resume(); }); })`
  ——续体必须经 `Post` 贴回执行线，不在任意完成线程直接跑业务。

### 1.4 `TAwaitable<F>`（P5 await 表达式）

定义：`Source/Common/Runtime/Async/Awaitable.h`

- **业务侧唯一 await 写法**：`TAwaitable<F>(args...)`。`F` 是 await 目标函数名
  （auto 非类型模板参数），`args` 是调用参数（仅类型构造，不存储）。
- 返回类型 `R` 由 `TAwaitableFnTraits` 从 `F` 的函数指针返回类型
  （`SFutureResult<R>::InnerType`）自动推导——**不用写 R / Args / decltype**。
- 占位转换 `operator ResultType()` / `operator RetType()`：让业务体在 codegen 解析时能编译
  （`int R = TAwaitable<F>(args);` 与 `return TAwaitable<F>(args);`）。
- 运行时真正的 await 由 MHeaderTool 生成的状态机实现覆盖（注入 `AsAwaiter()`）；业务编译
  时占位转换不参与运行（函数定义来自 mgenerated 状态机，见 §4）。

## 2. 标记：`MFUNCTION(..., Async)`

- **含义**：本方法 / 自由函数是异步方法——允许函数体内出现 await；返回 `SFutureResult<T>`；
  返回的 future **允许未 Ready**（运行时 dispatch 必须处理 pending 再回包）。
- **统一入口**：类成员 `MFUNCTION(ServerCall, Async)` / `MFUNCTION(Async, CallClient)` 与
  namespace-scope 自由函数 `MFUNCTION(Async)` 共用同一标记；不再有独立的 `MASYNC` 宏。
- **自由函数限制**：`MFUNCTION(Async)` 不允许带 `ServerCall` / `ClientCall` / `RPC` 等
  transport tag——MHeaderTool 对该误用报错。
- **工具强制规则（MHeaderTool）**：
  1. await（`AWAIT` / `AWAIT_OK` / `TAwaitable`）只能出现在 `MFUNCTION(..., Async)` 函数内；
  2. 被 await 的表达式类型必须是 `SFutureResult<T>`；
  3. Async 函数的返回类型必须是 `SFutureResult<T>`。
- 无 `Async` 标记的函数返回 `SFutureResult<T>` 时，约定**返回时已 Ready**（同步短路径）。
- 非 Async 函数可消费异步 API：用 `Then`，或在允许阻塞的上下文用 `Get()` / `Wait()`。
  只要求**线性 await 链**上的函数标 Async，不要求全调用栈 Async。

## 3. 业务写法

### 3.1 `AWAIT_OK(expr)`（宏语义）

宏定义：`Source/Common/Runtime/Async/AwaitMacros.h`

```cpp
#define AWAIT_OK(expr) Frame->AwaitOk(expr)
```

语义（spec §7.1）：
- **Ok → 产出 `T`**（若 `expr` 是 `SFutureResult<T>` 即得 `T`）；
- **Err → 把外层 future 置为同一错误并结束状态机**（不继续后续语句）。

展开契约：`AWAIT_OK` 仅供状态机 Frame 局部作用域调用——调用处必须有局部变量 `Frame`
（Frame 是 MHeaderTool 生成的 `MHeaderTool_AsyncFrame_<Class>_<Func>` 结构）。未就绪时挂起：
`Awaited.Then([Frame, Ctx](F){ Ctx->Post([Frame, F]{ Frame->Resume(); }); })`。

### 3.2 P5 业务形态：`TAwaitable`

P5 之后业务 async 函数体像 C# async 一样线性书写，await 点用 `TAwaitable<F>(args...)`
（codegen 解析后等价于 AWAIT_OK 语义，由状态机驱动）：

```cpp
// 业务头：纯声明（普通函数外观）
MFUNCTION(Async)
SFutureResult<int> AwaitDemoCompute(int Seed);

// await 目标（普通函数，返回 ready 的 SFutureResult<int>）
SFutureResult<int> AwaitDemoHelper(int V);

// 业务体：放在 *.Async.cpp（见 §4）
MFUNCTION(Async)
SFutureResult<int> AwaitDemoCompute(int Seed)
{
    int Mid = Seed * 3;                                       // 业务逻辑 1
    int R = TAwaitable<AwaitDemoHelper>(Mid);                 // await 点
    return SFutureResult<int>(TResult<int, FAppError>::Ok(R + 1));  // 业务逻辑 2
}
```

### 3.3 同步屏障 `Get()` / `Wait()`（≈ Task.Result）

- 合法用于：测试、工具、明确的同步适配层；用于**终结**向上的 await 传染。
- **红线**：在事件循环 / 处理当前入站包的线程上，禁止对"尚未 Ready、且其完成依赖本循环
  继续跑"的 future 阻塞 `Get` / `Wait`（死锁模式：`Loop 线程 → Handler → Get(等 RPC) →
  RPC 完成回调需 Loop 处理 → 永不完成`）。
- `SFutureResult::Get()` 内置检测：未就绪时若 `MAsyncContext::Current()->IsSameContext()`
  → DEBUG `assert(false)` + `LOG_ERROR("deadlock risk ...")`（RELEASE 至少 LOG_ERROR）。
- 状态机内部等待只用 await，不用 `Get`。

## 4. `*.Async.cpp` 文件约定（spec §7.2.1）

**async 业务函数体放 `Xxx.Async.cpp`——文件名即 codegen 专用源标记**：

| 文件 | 内容 | 业务编译 |
|---|---|---|
| `Xxx.h` | 类 / 函数声明 + `MFUNCTION(Async)`（MHeaderTool 扫描注册） | ✅ 参与 |
| `Xxx.Async.cpp` | **async 业务函数体**（`TAwaitable<...>` 体），**零 `#ifdef`** | ❌ 不编译（定义由 mgenerated 状态机提供） |
| `Xxx.cpp` | 普通实现（await 目标、辅助函数） | ✅ 参与 |

- 机制：MHeaderTool 源收集 = compile_commands ∪ 递归扫描 `*.Async.cpp`；解析时自动
  `-DMESSION_AWAIT_CODEGEN_SOURCE`（`ASTPipeline::MCodegenSourceCompilationDatabase` 对不在
  compile_commands 的文件给 fallback 命令）。函数定义全部来自 `.AwaitImpl.mgenerated.cpp`。
- 业务编译不编 `.Async.cpp`——避免与生成的 mgenerated 状态机实现重复定义（ODR）。
- **已废弃旧写法**：每个函数用 `#ifdef MESSION_AWAIT_CODEGEN_SOURCE` / `#endif` 包裹
  （每函数两行样板，且 async 体散在业务源里）。

## 5. 支持的控制流能力（P5）

由 `Source/Tools/MHeaderTool/Generation/CodeGenerator.h` 的**通用递归控制流生成器**
（AsyncBody → 控制流树 → 递归生成状态机）实现，已全部端到端验证：

- 顺序语句；任意层 `if` / `else` / `else-if` 链；
- `for` / `while` 循环体内 await；任意嵌套与组合（已验证至 3 层嵌套）；
- 分支内 early return；
- **fall-through**（if 块内 await 无 return → 恢复后继续外层语句）；
- 字符串字面量注释保护；非 int 返回类型（MString）；await 次数无上限。

仍然禁止：
- 跨 await 的 `goto`；
- 完整 try/catch 跨 await；
- Async 函数内对"需本 Context 推进"的 future 调用阻塞 `Get()`（§3.3 红线）。

## 6. 已验证范围

- **回归测试**：`ctest --test-dir Build -R AwaitCodegen`（`AwaitCodegenTest`，**31 断言**，
  15 个 TEST_CASE），覆盖：单 await / 多 await 串行 / 循环累加 / **真异步挂起-恢复**
  （挂起 ≥40ms 断言）/ **并发并行**（50ms vs 串行 100ms，总耗时 <90ms 断言）/ 类成员 async /
  if + early return / else await / if 内多 await / else-if 链 / 嵌套 if / 3 层嵌套 /
  分支内循环 / 循环内 if / fall-through / 非 int（MString）。
- **端到端演示**：AwaitDemo 可执行（`Source/Examples/AwaitDemo/`），`main` 依次验证单 await、
  多 await 串行、循环累加、两次挂起/恢复（≈100ms）、并发切换（≈50ms 不阻塞）、类成员 async、
  分支/else/嵌套/fall-through 等并打印结果。

已知边界：真实网络 await 链路（EchoAwait `ServerCall + Async` 生成路径已验证，但
Registry + Echo×2 + Gateway 拓扑的完整 RPC future 链路未端到端跑过）。

## 7. 相关实现

关键文件：

| 路径 | 角色 |
|---|---|
| `Source/Common/Runtime/Async/MAsync.h` | `SFutureResult<T>` / `FFutureResultError` / `SAwaiter` / `_unwrap` / `WrapAsSFutureResult` |
| `Source/Common/Runtime/Async/AsyncContext.h` / `.cpp` | `MAsyncContext` / `MLoopAsyncContext`（TLS ambient） |
| `Source/Common/Runtime/Async/Awaitable.h` | `TAwaitable<F>`（P5 await 表达式 + 占位转换） |
| `Source/Common/Runtime/Async/AwaitMacros.h` | `AWAIT_OK(expr)` 宏 |
| `Source/Common/Runtime/Async/IExecutor.h` | `mession::async::IExecutor`（MLoopAsyncContext 绑定的执行线抽象） |
| `Source/Common/Runtime/Concurrency/Promise.h` | `MPromise` / `MFuture`（底层 future） |
| `Source/Tools/MHeaderTool/**` | codegen：`MFUNCTION(Async)` 扫描、`*.Async.cpp` 收集、状态机生成、强制规则诊断 |
| `Source/Tools/MHeaderTool/Generation/CodeGenerator.h` | 通用递归控制流状态机生成器 |
| `Source/Examples/AwaitDemo/*` | 端到端演示（`*.Async.cpp` 业务体、Branch/Complex/Member/String demo、`main`） |
| `Source/Examples/AwaitDemo/Tests/AwaitCodegenTest.cpp` | 回归测试（31 断言，`ctest -R AwaitCodegen`） |
| `Source/Examples/AsyncDemo/` | 早期单 await 独立示例 |

spec 来源（设计讨论 / 决策过程 / 历史分期存档，不在本文档重复）：
- `Docs/superpowers/specs/2026-07-24-cpp17-async-await.md`（父 spec）
- `Docs/superpowers/specs/2026-07-25-cpp17-p0-cleanup.md`（P0）
- `Docs/superpowers/specs/2026-07-28-async-p4-wrap.md`（P4）
- `Docs/superpowers/specs/2026-07-29-async-demo-example-design.md`（AsyncDemo 设计）
- `Docs/superpowers/specs/2026-08-13-async-p5-implementation-status.md`（P5 状态）
