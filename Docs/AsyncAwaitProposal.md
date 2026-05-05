# Async/Await 系统设计提案

> 提案时间：2026-05-04
> 目标：为 Mession 提供 C++17 风格的 async/await 异步编程能力

---

## 一、现状问题

### 1.1 返回类型嵌套过深

现有 async 函数返回类型：

```cpp
MFuture<TResult<FPlayerLogoutResponse, FAppError>>
    PlayerLogout(const FPlayerLogoutRequest& Request);
```

- `MFuture` 是异步容器
- `TResult<T, FAppError>` 是错误容器
- 嵌套三层，签名臃肿，难以阅读

对比 C#：

```csharp
Task<FPlayerLogoutResponse> PlayerLogout(FPlayerLogoutRequest Request);
```

差距明显。

### 1.2 缺少统一 async 语法

现有代码异步写法分散：
- `MAwait(Future)` — fiber 感知等待，但函数调用语法
- `Future.Then(lambda)` — 手动续体链，容易嵌套过深
- 无 `co_await` / `co_return` 关键字，纯续体模拟

### 1.3 错误处理不统一

- `MFuture<T>` 的 `Get()` 对 `TResult<T, E>` 特化时会 throw
- 但各模块错误传播方式不统一
- `FPlayerCommandError` 只能在 fiber 内传播

---

## 二、设计目标

1. **简化返回类型** — `SFutureResult<T>` 替代 `MFuture<TResult<T, FAppError>>`
2. **MHeaderTool 代码生成** — 复用现有 MHeaderTool 管线生成续体链状态机
3. **统一错误传播** — 所有 async 函数通过 `SFutureResult<T>` 的 `Get()` 自动处理错误
4. **集成反射** — 复用现有 `MFunction` 宏注册，新增 `MFunction(Async)` 标记
5. **C++17 兼容** — 不使用 C++20 coroutine，纯代码生成 + lambda

---

## 三、核心类型设计

### 3.1 SFutureResult<T>

```cpp
template<typename T>
struct SFutureResult : MFuture<TResult<T, FAppError>>
{
    using Super = MFuture<TResult<T, FAppError>>;
    using Super::Super;

    // 抛出 FFutureResultError（统一错误类型）
    T Get() const
    {
        const TResult<T, FAppError>& Result = Super::Get();
        if (Result.IsErr())
        {
            throw FFutureResultError(Result.GetError());
        }
        return Result.GetValue();
    }

    // 原样返回 TResult，不抛
    TResult<T, FAppError> GetResult() const
    {
        return Super::Get();
    }

    bool IsOk() const { return GetResult().IsOk(); }
    bool IsErr() const { return GetResult().IsErr(); }
    const FAppError& GetError() const { return GetResult().GetError(); }
};
```

**语义约定：**
- `Get()` — 抛出统一错误类型，`MAwait(SFutureResult<T>)` 内部调用此方法
- `GetResult()` — 返回原始 TResult，供需要判断错误的场景使用

### 3.2 FFutureResultError

```cpp
class FFutureResultError : public std::exception
{
public:
    explicit FFutureResultError(FAppError InError)
        : Error(std::move(InError))
        , Message(Error.Code.empty() ? Error.Message : Error.Code + ": " + Error.Message)
    {
    }

    const char* what() const noexcept override { return Message.c_str(); }
    const FAppError& GetError() const { return Error; }

private:
    FAppError Error;
    MString Message;
};
```

**用途：** 替代现有 `FPlayerCommandError`，统一所有 async 函数错误类型。

### 3.3 类型别名宏

```cpp
// SFutureResult<T> 的别名，简化声明
#define MFUTURE(T) SFutureResult<T>

// 用法
MFunction(ServerCall)
MFUTURE(FPlayerLogoutResponse) PlayerLogout(const FPlayerLogoutRequest& Request);
```

---

## 四、MFunction(Async) — MHeaderTool 代码生成

### 4.1 核心思路

**复用 MHeaderTool 管线**，不引入新的宏 ceremony。

MHeaderTool 已有完整能力：
- 扫描头文件，解析函数声明和函数体原文
- 生成 `.mgenerated.cpp` 胶水代码

只需新增 `MFunction(Async)` 标记，MHeaderTool 识别后生成续体链状态机。

### 4.2 用户写法

```cpp
// 头文件中：只需要声明 + 函数体，MHeaderTool 自动生成状态机
MFunction(Async)
MFUTURE(FPlayerLogoutResponse) PlayerLogout(FPlayerLogoutRequest Request)
{
    auto* Profile = AWAIT(ResolveProfile());
    AWAIT(SaveProfile(Profile));
    co_return FPlayerLogoutResponse{};
}
```

对比其他语言：

| 语言 | 声明 | 实现 |
|------|------|------|
| C# | `async Task<T> Foo()` | 编译器生成状态机 |
| Mession | `MFunction(Async) MFUTURE(T) Foo()` | MHeaderTool 生成状态机 |

### 4.3 MHeaderTool 生成的状态机

```cpp
// Build/Generated/PlayerService.mgenerated.cpp 中
MFUTURE(FPlayerLogoutResponse) PlayerLogout(FPlayerLogoutRequest Request)
{
    struct SState {
        FPlayerLogoutRequest Request;

        template<typename FCallback>
        void _run(FCallback&& _onComplete) {
            ResolveProfile().Then([this, _onComplete](auto _r0) {
                auto _v0 = _unwrap(_r0);
                if (_v0.IsErr()) { _onComplete(_v0); return; }
                auto* Profile = std::move(_v0).GetValue();

                SaveProfile(Profile).Then([this, _onComplete](auto _r1) {
                    auto _v1 = _unwrap(_r1);
                    if (_v1.IsErr()) { _onComplete(_v1); return; }
                    _onComplete(TResult<FPlayerLogoutResponse, FAppError>::Ok(FPlayerLogoutResponse{}));
                });
            });
        }
    };

    static SState State{Request};
    MPromise<TResult<FPlayerLogoutResponse, FAppError>> Promise;
    State._run([&](auto R) { Promise.SetValue(std::move(R)); });
    return Promise.GetFuture();
}
```

### 4.4 AWAIT / co_return 语义

- `AWAIT(expr)` — 提取 expr 中的 future，生成 `.Then(lambda)` 续体
- `co_return value` — `_onComplete(TResult::Ok(value))`
- `co_return err(code, msg)` — `_onComplete(TResult::Err(FAppError{...}))`

错误自动短路传播，无需每个 AWAIT 后面判空。

### 4.5 内部工具函数

由 `MAsync.h` 提供（MHeaderTool 生成时引用）：

```cpp
// 解包任意 Future → TResult（由 MAsync.h 提供）
template<typename T>
auto _unwrap(const MFuture<T>& F) -> TResult<...>;
```

### 4.6 解析算法

MHeaderTool 内部对函数体文本做字符串扫描：

```
输入: AWAIT(f1()) + AWAIT(f2(r1)) + co_return r2
步骤:
1. 从左到右扫描，找出所有 AWAIT(expr) 位置
2. 提取每个 AWAIT 括号内的 future 表达式
3. 提取 co_return 后的表达式
4. 反向拼接续体链（最后一个 AWAIT 的 lambda 末尾是 co_return）
```

---

## 五、MEventAwait — 事件可等待

### 5.1 需求

在 async 函数内等待一个事件触发：

```cpp
MFunction(Async)
MFUTURE(FPlayerLoginEvent) WaitForLogin(uint64 PlayerId)
{
    FPlayerLoginEvent Event;
    AWAIT_EVENT(Event);
    co_return Event;
}
```

### 5.2 实现

```cpp
namespace MEventBus
{
    template<typename TEvent>
    class TEventAwaiter
    {
    public:
        explicit TEventAwaiter(TEvent& OutEvent)
            : OutEvent(&OutEvent)
        {}

        bool await_ready() const
        {
            return !MHasCurrentPlayerCommand();
        }

        void await_suspend(TFunction<void()> Resume)
        {
            MEventSubscription Sub = MEventBus::SubscribeOnce<TEvent>(
                nullptr,
                [this, Resume](const TEvent* E) mutable {
                    *OutEvent = *E;
                    Resume();
                });
            (void)Sub;
        }

        TEvent& await_resume() const { return *OutEvent; }

    private:
        TEvent* OutEvent = nullptr;
    };

    template<typename TEvent>
    TEventAwaiter<TEvent> Await(TEvent& OutEvent)
    {
        return TEventAwaiter<TEvent>(OutEvent);
    }
}

// 简化写法宏
#define AWAIT_EVENT(Event) \
    co_await MEventBus::Await(Event)
```

> 注意：`co_await MEventBus::Await<T>(event)` 需要 MHeaderTool 识别 `TEventAwaiter<T>` 类型，在生成续体链时走特殊路径。

---

## 六、文件结构

```
Source/Common/Runtime/Async/
├── MAsync.h           — SFutureResult<T> + MFUTURE 宏 + FFutureResultError
└── MEventAwait.h     — MEventBus::Await<T>(TEvent&) + TEventAwaiter
```

**修改文件：**
- `CMakeLists.txt` — 注册新源文件
- `Source/Common/Runtime/Concurrency/FiberAwait.h` — 适配 SFutureResult
- `Source/Common/Runtime/Event/MEventBus.h` — 增加 Await<T> 方法
- `Source/Tools/MHeaderTool.cpp` — 新增 MFunction(Async) 代码生成

---

## 七、与其他模块的关系

| 模块 | 关系 |
|------|------|
| MFunction 反射 | `MFunction(Async)` 复用现有反射注册管线 |
| MFuture/MPromise | `SFutureResult` 继承 `MFuture`，`Get()` 行为特化 |
| MHeaderTool | 解析函数体，生成续体链状态机到 `.mgenerated.cpp` |
| FiberScheduler | async 函数执行在 fiber 内，续体 post 到 player strand |
| MEventBus | `MEventAwait` 桥接事件发布和 fiber 挂起 |

---

## 八、风险与限制

1. **AWAIT 表达式限制** — `AWAIT(expr)` 中 `expr` 必须是单个完整的 future 表达式
2. **不支持嵌套 AWAIT** — `AWAIT(AWAIT(f1()), f2())` 不支持
3. **调试困难** — 续体链的堆栈跟踪不直观
4. **MHeaderTool 依赖** — 函数体解析依赖字符串扫描，复杂表达式可能出错

---

## 九、优先级

1. **P0（核心）** — `MAsync.h`（SFutureResult + MFUTURE）+ MHeaderTool Async 生成
2. **P1（高）** — `MEventAwait.h`（事件可等待）
3. **P2（中）** — 适配现有 `MAwait` 迁移路径
