# Async/Await 实现任务清单

> 文档关联：`Docs/AsyncAwaitProposal.md`
> 优先级：P0 先做 MAsync.h，P1 做 MHeaderTool 生成，P2 做 MEventAwait.h

> **2026-05-05 状态：** 任务 1-4 已完成，Task 5 无需修改（无新增 .cpp），构建验证待用户环境 CMake 冲突解决后执行。

---

## 任务 1：MAsync.h — SFutureResult + MFUTURE 宏 ✅ 已完成

**文件：** `Source/Common/Runtime/Async/MAsync.h`（新建）

**内容：**
1. `FFutureResultError` 类
   - 继承 `std::exception`
   - 成员：`FAppError Error`，`MString Message`
   - `GetError()` 返回 `const FAppError&`
   - `what()` 返回 `Message.c_str()`
   - 构造函数 `explicit FFutureResultError(FAppError InError)`

2. `SFutureResult<T>` 模板 struct
   - 继承 `MFuture<TResult<T, FAppError>>`
   - `using Super = MFuture<TResult<T, FAppError>>`
   - `using Super::Super`（继承构造函数）
   - `Get()` — 调用 `Super::Get()`，若 `IsErr()` 抛 `FFutureResultError`
   - `GetResult()` — 直接返回 `Super::Get()`（不抛）
   - `IsOk()` — `GetResult().IsOk()`
   - `IsErr()` — `GetResult().IsErr()`
   - `GetError()` — `GetResult().GetError()`

3. `MFUTURE(T)` 宏
   - `#define MFUTURE(T) SFutureResult<T>`

**验证：**
- `MFUTURE(FPlayerLogoutResponse)` 展开为 `SFutureResult<FPlayerLogoutResponse>`
- `SFutureResult<int>::Get()` 在 err 时抛 `FFutureResultError`
- `SFutureResult<int>::GetResult()` 在 err 时不抛

---

## 任务 2：FiberAwait.h — 适配 SFutureResult ✅ 已完成

**文件：** `Source/Common/Runtime/Concurrency/FiberAwait.h`（修改）

**改动：**
1. `MAwaitOk(TPlayerCommandFuture<T> Future)` 改为使用 `SFutureResult<T>`
   - 改用 `SFutureResult<T>` 替代
   - `Get()` 抛 `FFutureResultError`，匹配 `FPlayerCommandError` 语义

2. 添加重载：
   ```cpp
   template<typename T>
   T MAwaitOk(SFutureResult<T> Future);
   ```

**验证：**
- `MAwaitOk(MFUTURE(FPlayerLogoutResponse){})` 编译通过
- `MAwaitOk()` 内部调 `Future.Get()`，err 时抛 `FFutureResultError`

---

## 任务 3：MHeaderTool — 添加 Async 函数代码生成 ✅ 已完成

**文件：** `Source/Tools/MHeaderTool.cpp`（修改）

**核心思路：**
MHeaderTool 已有完整的函数解析管线（`SParsedFunction` 包含 `Body` 字段存函数体原文）。新增对 `MFunction(Async)` 的识别和对应的 glue code 生成。

### 3.1 新增标记

在 `EParsedTypeKind` 后增加 `Async` 函数类型：
```cpp
// SParsedFunction 新增字段
struct SParsedFunction {
    // ... 现有字段 ...
    bool bIsAsync = false;
    std::string AsyncBody;  // 函数体原文（AWAIT / co_return 所在）
};
```

### 3.2 识别 `MFunction(Async)`

在 `ParseFunctionsInClassBody` 的 `MacroNames` 列表中增加：
```cpp
"MFunction(Async)",
```

解析时，识别宏参数中的 `"Async"` 标记，解析后续函数声明和函数体（花括号对匹配），存入 `AsyncBody`。

### 3.3 解析 AWAIT / co_return

在函数体文本中：
- 用正则或字符串扫描找出所有 `AWAIT(expr)` 模式
- 解析 `co_return expr` / `co_return` / `co_return err(...)`
- 提取 await 链顺序和变量依赖

### 3.4 生成 glue code

在 `WriteGeneratedSource` 中，对 `Async` 函数生成：

```cpp
// PlayerLogout 的状态机 glue code
MFUTURE(FPlayerLogoutResponse) PlayerLogout(const FPlayerLogoutRequest& Request)
{
    // 参数结构体
    struct SParams {
        const FPlayerLogoutRequest& Request;
    };
    static SParams Params{Request};

    // 状态机
    struct SState {
        SParams* P;

        template<typename FCallback>
        void _run(FCallback&& _onComplete) {
            // AWAIT 链解析后生成的续体嵌套代码
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

    static SState State{&Params};
    MPromise<TResult<FPlayerLogoutResponse, FAppError>> Promise;
    State._run([&](auto R) { Promise.SetValue(std::move(R)); });
    return Promise.GetFuture();
}
```

### 3.5 `_unwrap` 辅助函数

生成代码中注入统一解包函数：

```cpp
// 由 MAsync.h 提供
template<typename T>
auto _unwrap(const MFuture<T>& F) -> TResult<...>;
```

**验证：**
- MHeaderTool 处理包含 `MFunction(Async)` 的头文件不报错
- 生成的 `.mgenerated.cpp` 编译通过
- AWAIT 链正确生成续体嵌套
- 错误自动短路传播

---

## 任务 4：MEventAwait.h — 事件可等待 ✅ 已完成

**文件：** `Source/Common/Runtime/Event/MEventAwait.h`（新建）

**内容：**

1. `TEventAwaiter<TEvent>` 类
   - `await_ready()` — 不在 fiber 内返回 true（同步执行）
   - `await_suspend(TFunction<void()> Resume)` — 一次性订阅，触发时调 Resume
   - `await_resume()` — 返回事件引用

2. `MEventBus::Await<TEvent>(TEvent& OutEvent)` 工厂方法

3. `AWAIT_EVENT(Event)` 宏（简化写法）

**验证：**
- `AWAIT_EVENT(MyEvent)` 在 fiber 内挂起等待
- 事件触发后自动 resume
- 不在 fiber 内时同步返回

---

## 任务 6：集成测试（待执行）

### 6.1 单元测试

**测试文件：** `Source/Common/Runtime/Async/MAsync.test.cpp`（新建）

覆盖场景：
- `SFutureResult<int>::Get()` 正常值 → 返回值
- `SFutureResult<int>::Get()` err 值 → 抛 `FFutureResultError`
- `SFutureResult<int>::GetResult()` err 值 → 不抛，返回 TResult
- `MFUTURE(T)` 宏展开正确

### 6.2 集成测试

改造 `PlayerLogout` 或 `PlayerGrantExperience` 为 `MFunction(Async)` 写法，验证：
- 编译通过
- 错误通过 `co_return err()` 正确传播
- 多个 AWAIT 链式执行

---

## 执行顺序

```
1. MAsync.h（SFutureResult + MFUTURE）              ✅
2. FiberAwait.h（适配 SFutureResult）               ✅
3. MHeaderTool.cpp（新增 MFunction(Async) 代码生成） ✅
4. 集成测试（改造 PlayerLogout）                    ⏳ 待执行
5. MEventAwait.h（事件可等待）                     ✅
6. CMakeLists.txt 更新                             ✅ 无需修改
7. cmake --build Build 编译                         ⏳ 用户环境 CMake 冲突
8. MHeaderTool 重新生成                             ⏳ 待执行
9. validate.py 验证                                 ⏳ 待执行
```
5. MEventAwait.h（事件可等待）
       ↓
6. CMakeLists.txt 更新
       ↓
7. cmake --build Build 编译
       ↓
8. MHeaderTool 重新生成
       ↓
9. validate.py 验证
```

---

## 验收标准

1. `MFUTURE(FPlayerLogoutResponse)` 替代原有 `MFuture<TResult<FPlayerLogoutResponse, FAppError>>` 签名
2. `MFunction(Async)` 声明的函数由 MHeaderTool 生成状态机 glue code
3. AWAIT 链中任意步骤 err，自动短路，不继续执行后续 AWAIT
4. `SFutureResult<T>::Get()` err 时抛 `FFutureResultError`
5. `MEventBus::Await<T>()` 在 fiber 内正确挂起和恢复
6. 全量编译通过
7. `validate.py` 全量验证通过
