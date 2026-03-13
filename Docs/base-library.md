# 基础库说明

> 游戏/服务端共用类型与工具，集中在 Core 与 Common，新代码优先使用项目别名与封装，避免直接使用 STL 类型名（见 [.cursor/rules/project-cpp-style.mdc](../.cursor/rules/project-cpp-style.mdc)）。

## Core（Core/NetCore.h）

- **类型别名**：`uint8`～`uint64`、`int8`～`int64`、`float32`、`double64`；`FString`/`TString`、`TArray`/`TByteArray`、`TVector`、`TMap`、`TList`、`TQueue`、`TStack`、`TDeque`、`TSet`、`TMultiSet`、`TMultiMap`、`TUnorderedMap`、`TUnorderedSet`、`TUnorderedMultiMap`、`TUnorderedMultiSet`、`TStringView`（C++17）、`TSharedPtr`/`TWeakPtr`/`TUniquePtr`、`TFunction`、`TOptional`、`TPair`、`TIfstream`/`TOfstream`。
- **智能指针**：`MakeShared<T>(...)` 统一构造 `TSharedPtr`。
- **字节序**：`HostToNetwork` / `NetworkToHost`（uint16/32/64）。
- **数学**：`SVector`（含 `Size`、`SizeSquared`、`Normalized()`、`Dot(V)`）、`Distance(A, B)`、`SRotator`、`STransform`；`Clamp(float, float, float)`、`Lerp(float, float, float)`、`Lerp(SVector, SVector, float)`。
- **时间**：`MTime::GetTimeSeconds()`、`SleepSeconds`/`SleepMilliseconds`。
- **错误返回**：`TResult<T, E>`、`TResult<void, E>`（`IsOk`/`IsErr`、`GetValue`/`GetError`）。
- **其他**：`MUniqueIdGenerator::Generate()`、`MNonCopyable`；C++20 下 `TSpan<T>`、`TSpanMutable<T>`。
- **环形缓冲区（Core/RingBuffer.h）**：`MRingBuffer<T>(capacity)`，定长、满时覆盖最旧；`PushBack`、`PopFront`（返回 `TOptional<T>`）、`Front`/`Back`、`At(i)`、`GetSize`/`GetCapacity`、`Empty`/`Full`、`Clear`。

## Common

- **Config（Common/Config.h）**：`MConfig::LoadFromFile`、`GetStr`/`GetInt`/`GetU16`/`GetU32`/`GetU64`/`GetBool`、`GetEnv`/`GetEnvInt`、`ApplyEnvOverrides`。
- **字符串（Common/StringUtils.h）**：`MString::ToString(...)`、`TrimInPlace`、`TrimCopy`、`Split`、`Join`。**TStringView 工具（C++17）**：`MStringView::TrimView(View)`、`MStringView::ToFString(View)`、`StartsWith`、`EndsWith`、`Contains`。
- **日志**：`Common/Logger.h`、`Common/LogSink.h`；`LOG_DEBUG`/`LOG_INFO`/`LOG_WARN`/`LOG_ERROR`/`LOG_FATAL`，`MLogger::DefaultLogger()`、`LogStartupBanner`/`LogStarted`。
- **协议与消息**：`Common/MessageUtils.h`（`MMessageWriter`/`MMessageReader`）、`Common/ServerMessages.h`（各 `S*Message`、`ParsePayload`、`BuildPayload`、`SendTypedServerMessage`）。
- **服务器连接**：`Common/ServerConnection.h`（`MServerConnection`、`EServerMessageType` 等）。
- **命令行**：`Common/ParseArgs.h`（`MParseArgs::Parse`）。

## 网络层（Core）

- **Socket**：`Core/Socket.h`、`SocketPlatform.h`、`SocketAddress.h`、`SocketHandle.h`、`PacketCodec.h`；`MSocket::CreateListenSocket`、`MTcpConnection`、`MLengthPrefixedPacketCodec`。
- **轮询**：`Core/Poll.h`、`MSocketPoller`。
- **主事件循环**：`Core/MEventLoop.h`（`MEventLoop`）；容纳若干**子 EventLoop**（`IEventLoopStep`），每帧按注册顺序依次执行各子的 `RunOnce(timeoutMs)`。服务器只驱动 `MasterLoop.RunOnce()`，不再直接调用各子循环。
- **子循环接口**：`Core/IEventLoopStep.h`（`IEventLoopStep`）；子类实现 `RunOnce(int timeoutMs)`，由主循环每帧调用。
- **网络子循环**：`Core/EventLoop.h`（`MNetEventLoop`）；实现 `IEventLoopStep`，仅负责监听 + 连接 poll + 可读分发。
- **任务子循环**：`Core/ITaskRunner.h`（接口）、`Core/TaskEventLoop.h`（`MTaskEventLoop`）；实现 `ITaskRunner` 与 `IEventLoopStep`，`PostTask` 投递任务，`RunOnce` 执行任务队列；通常注册为第一步（timeout 0），网络循环为第二步（timeout 16）。

## 异步与并发（Core）

- **Promise/Future**：`Core/Promise.h`；`MPromise<T>`、`MFuture<T>`（含 `void` 特化）；`SetValue`/`SetException`、`Get`/`Wait`/`Then`。单消费者，线程安全。
- **任务队列与线程池**：`Core/TaskQueue.h`（`MTaskQueue`：Push/TryPop/Pop/Shutdown）、`Core/ThreadPool.h`（`MThreadPool`：Submit/Shutdown，固定 N 工作线程）。
- **协程风格（库级，C++17）**：`Core/Async.h`；`MAsync::Yield(runner, next)` 下一 tick 执行 `next`（`runner` 为 `ITaskRunner*`，如 `GetTaskRunner()`）；`MAsync::MSequence::Create(runner)` → `Do(step)`、`Run()`，步与步之间自动在下一 tick 衔接。详见 `docs/async-promise-coroutine.md`、`docs/taskqueue-threadpool.md`。

## 使用建议

1. 数值转字符串用 `MString::ToString(...)`，不直接写 `std::to_string`。按分隔符拆/拼用 `MString::Split` / `MString::Join`。
2. 需要“集合、无顺序、O(1) 查找”时用 `TUnorderedSet`；需要有序集合用 `TSet`。允许多个相同键用 `TMultiMap`/`TMultiSet` 或 `TUnorderedMultiMap`/`TUnorderedMultiSet`。LIFO 用 `TStack`，双端队列用 `TDeque`。定长 FIFO 且满时覆盖最旧用 `MRingBuffer<T>`。只读字符串参数用 `TStringView`，操作用 `MStringView::TrimView`/`StartsWith`/`EndsWith`/`Contains`（C++17）。
3. 可能失败的操作返回 `TResult<T, FString>`，调用方检查 `IsOk()` 并处理 `GetError()`。
4. 配置读取统一用 `MConfig::Get*`，避免手写 `atoi`/`strtol`。
