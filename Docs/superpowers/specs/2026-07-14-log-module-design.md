# Mession 日志模块设计

**日期:** 2026-07-14
**状态:** 设计稿(待评审)

## 1. 背景与目标

### 1.1 现状

`Source/Common/Runtime/Log/` 现有 540 行实现:

- `MLogger` 单例 + 命名注册表
- 同步路径:`va_list` → 4096 字节栈缓冲 `vsnprintf` → 模式串 `FormatLine`(局部时间字符串 `localtime`)→ 对每个 Sink 加锁 `Write`
- `MFileSink` 单文件追加 + 行锁,无 rotate/无缓冲
- `MConsoleSink` 锁保护 `std::cout`
- 宏 `LOG_INFO/DEBUG/WARN/ERROR/FATAL` → `MLogger::DefaultLogger()->Log(...)`,带 `__FILE__/__LINE__/__func__`,printf 风格

### 1.2 性能瓶颈

1. 调用点持锁(`Mutex`)串行化所有 sink 写盘
2. `localtime` + `strftime` 在热路径(每条都调一次)
3. `vsnprintf` 用栈缓冲,大消息截断
4. 文件 sink 无缓冲,每次 `<<` 一次 syscall
5. 无 Category 概念,21 个调用点全部走默认 logger

### 1.3 目标

- 吞吐 ≥ 100 万条/秒(8 线程并发)
- p99 emit 延迟 < 1µs(异步 enqueue 命中 L1)
- 支持 Category(UE 风格,可运行时调 Level / 静默 / 路由)
- 按大小/时间滚动 + 远程 UDP/TCP 收集
- FATAL 级同步采集最近 N 条 + 系统信息 + 触发 core dump(预留 Coredump 平台上报 hook)
- 计数器(`MLogMetrics`)供后续 Metrics 模块读取
- **API 破坏接受**(用户已确认):旧 `MLogger` / `ILogSink` / `LOG_INFO` 等宏替换为新 API;旧 `LOG_*` 宏签名保留,内部映射到 `MLog::Write(LogCore, ...)`

## 2. 命名规范

沿用项目约定:

| 类别 | 命名 | 示例 |
|---|---|---|
| 接口 | `I*` | `ILogSink` |
| 类 | `M*` | `MLogDispatcher`、`MLogSinkWriter`、`MLogContext`、`MLogRegistry`、`MLogRouter`、`MLogMetrics`、`MLogStringTable`、`MConsoleSink`、`MRollingFileSink`、`MUdpSink`、`MTcpSink`、`MCoredumpSink` |
| 结构体 | `S*` | `SLogRecord`、`SLogCategory`、`SSinkConfig`、`SLogContextSnapshot`、`SInitParams` |
| 枚举 | `E*` | `ELogLevel`、`EEvictionPolicy`、`EFlushPolicy` |
| 模板容器 | `T*` | `TMpscRingBuffer<T>`、`TBatch<T, N>` |
| 布尔字段 | `bXxx` | `bSuppressed`、`bUseColor`、`bAsync` |
| 函数 | PascalCase | `Enqueue`、`Flush`、`Drain`、`Write` |
| 命名空间 | `MLog` | facade |
| 宏 | `LOG_*` / `*_LOG` | `LOG_INFO`、`NET_LOG`、`DECLARE_LOG_CATEGORY_EXTERN`、`DEFINE_LOG_CATEGORY` |

## 3. 架构总览

### 3.1 顶层数据流

```
                                MLogRegistry(分类/ID/初始Level)
                                       │
                                       ▼
[业务线程] ─► Filter(级别+静默) ─► Router(取 SinkMask) ─► TMpscRingBuffer<SLogRecord> ─► MLogDispatcher ─┬─► MConsoleSink::Writer
                                                                                                       ├─► MRollingFileSink::Writer
                                                                                                       ├─► MUdpSink::Writer
                                                                                                       └─► MTcpSink::Writer

[业务线程 FATAL 级] ─► 同 Filter/Router,但路径 = MCoredumpSink::HandleFatal(同步采集,不经过队列)
[MCoredumpSink]        ─◄── FATAL 触发,同步收集最近 N 条 + 系统信息 + raise(SIGABRT)
```

### 3.2 核心组件

1. **`SLogRecord`(POD,固定 64B)** — 队列里流转的最小单位。字段:`TimestampNs`、`ThreadId`、`CategoryId`、`Level`、`Flags`、`FileStringId`、`Line`、`FuncStringId`、`SinkMask`、`ContextSnapshotId`、消息(`Inline[16B]` 或 `Overflow`)。
2. **`TMpscRingBuffer<SLogRecord>`** — 无锁多生产者单消费者环形队列。固定容量 1<<20 = 1M 条 ~64MB(可配)。`Enqueue` 只做 `atomic_fetch_add` + memory copy。
3. **`MLogContext`(TLS, `MLogContextScope` RAII)** — 每线程一份,提供 `Set("actor_id", ...).Set("req_id", ...)`。提交时一次性快照(`swap(Empty)`),后台线程不再触碰 TLS。
4. **`MLogRegistry` 全局注册表** — 启动期按 `DEFINE_LOG_CATEGORY` 注册,`Id` 紧凑分配。提供 `GetById(Id)`。
5. **`MLogRouter` 路由表** — 启动期构建,运行时单写多读(写时拷一份 swap,无锁)。按 Category → SinkMask + MinLevel 决策。
6. **`MLogFilter` 过滤** — 读 Category 的 `bSuppressed` 与 `RuntimeLevel`,无锁,不入队就丢弃。
7. **`MLogDispatcher` 线程** — 单消费者,批量 pop(默认 256 条或 64KB)。按 sink 维度 fan-out,投递到各 `MLogSinkWriter::Inbox`(每 sink 一个独立 MPSC)。
8. **`MLogSinkWriter` 线程(每 sink 一个)** — 从自己的 inbox 取 batch → 序列化为目标格式(Console 文本 / UDP JSON Lines)→ 写目标 → 触发 Flush(10ms 定时或 64KB 缓冲阈值)。
9. **Sinks**:
   - `MConsoleSink` — stdout,`bUseColor` 可选,批 write
   - `MRollingFileSink` — 按大小(默认 100MB)滚动,保留 `NumArchives` 个
   - `MUdpSink` / `MTcpSink` — JSON Lines 发远程 collector
   - `MCoredumpSink` — FATAL 同步路径(详见 §6)
10. **`MLogMetrics`** — 原子计数器,只读 `Snapshot()` 返回 `SLogMetricsSnapshot`。字段:`Enqueued`、`DroppedEvicted`、`DroppedOverflow`、`BlockedEnqueues`、`DispatchedBatches`、`WrittenBytesPerSink`、`SuppressedByCategory[]`。

### 3.3 关键不变量

- **异步吞吐**:Info/Debug/Trace 走非阻塞 enqueue,满时按 `EEvictionPolicy` 处理(默认 `DropOldest`,但 ERROR+ 不被覆盖)
- **同步可靠性**:Error/Critical 走 `BlockingEnqueue`(不用于 FATAL,FATAL 走 `MCoredumpSink`)+ `FlushBarrier`
- **shutdown 顺序**:`MLogDispatcher::Stop()` → drain → 各 `MLogSinkWriter::Stop()` → drain → `TMpscRingBuffer::~SLogRecord()`
- **异步信号安全**:信号处理函数内禁用 `LOG_*` 宏,允许 `write(2)` 到 stderr

## 4. Category 模型(UE 风格)

### 4.1 声明/定义宏(头/cpp 分离)

```cpp
// Common/Runtime/Log/LogCategory.h
#define DECLARE_LOG_CATEGORY_EXTERN(Name, DefaultLevel) \
    extern SLogCategory Name;

#define DEFINE_LOG_CATEGORY(Name) \
    SLogCategory Name = *MLogRegistry::Get().RegisterOrGet(#Name, ELogLevel::DefaultLevel, &Name);
```

```cpp
// Common/Runtime/Log/LogCategories.h (项目级)
#pragma once
#include "Common/Runtime/Log/LogCategory.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCore,  ELogLevel::Info)
DECLARE_LOG_CATEGORY_EXTERN(LogNet,   ELogLevel::Info)
DECLARE_LOG_CATEGORY_EXTERN(LogDb,    ELogLevel::Warn)
DECLARE_LOG_CATEGORY_EXTERN(LogRpc,   ELogLevel::Info)
DECLARE_LOG_CATEGORY_EXTERN(LogAuth,  ELogLevel::Info)
DECLARE_LOG_CATEGORY_EXTERN(LogScene, ELogLevel::Info)

// Common/Runtime/Log/LogCategories.cpp
#include "Common/Runtime/Log/LogCategories.h"
DEFINE_LOG_CATEGORY(LogCore)
DEFINE_LOG_CATEGORY(LogNet)
DEFINE_LOG_CATEGORY(LogDb)
DEFINE_LOG_CATEGORY(LogRpc)
DEFINE_LOG_CATEGORY(LogAuth)
DEFINE_LOG_CATEGORY(LogScene)
```

### 4.2 Category 内部表示

```cpp
struct SLogCategory
{
    const char*  Name;
    uint16       Id;              // 启动期分配,运行时索引 O(1)
    ELogLevel    DefaultLevel;    // DECLARE 时指定,只读
    std::atomic<ELogLevel> RuntimeLevel;  // 默认 = DefaultLevel,可运行时改
    std::atomic<bool>      bSuppressed;   // 默认 false
    std::atomic<uint64>    DropCount;
};
```

### 4.3 调用宏

**业务 Category 宏(新代码):**

```cpp
#define NET_LOG(Level, Fmt, ...)   MLog::Write(LogNet,   ELogLevel::Level, Fmt, __VA_ARGS__)
#define DB_LOG(Level, Fmt, ...)    MLog::Write(LogDb,    ELogLevel::Level, Fmt, __VA_ARGS__)
#define RPC_LOG(Level, Fmt, ...)   MLog::Write(LogRpc,   ELogLevel::Level, Fmt, __VA_ARGS__)
#define AUTH_LOG(Level, Fmt, ...)  MLog::Write(LogAuth,  ELogLevel::Level, Fmt, __VA_ARGS__)
#define SCENE_LOG(Level, Fmt, ...) MLog::Write(LogScene, ELogLevel::Level, Fmt, __VA_ARGS__)
```

**过渡宏(保留旧 `LOG_*` 形式):**

```cpp
#define LOG_INFO(...)  MLog::Write(LogCore, ELogLevel::Info,     __VA_ARGS__)
#define LOG_DEBUG(...) MLog::Write(LogCore, ELogLevel::Debug,    __VA_ARGS__)
#define LOG_WARN(...)  MLog::Write(LogCore, ELogLevel::Warn,     __VA_ARGS__)
#define LOG_ERROR(...) MLog::Write(LogCore, ELogLevel::Error,    __VA_ARGS__)
#define LOG_FATAL(...) MLog::Write(LogCore, ELogLevel::Critical, __VA_ARGS__)
```

**FATAL 同步宏(走 `MCoredumpSink::HandleFatal`):**

```cpp
#define LOG_FATAL_EX(Cat, Fmt, ...) \
    do { \
        va_list __ap; va_start(__ap, Fmt); \
        MLog::HandleFatal((Cat), Fmt, __ap); \
        va_end(__ap); \
    } while (0)
```

## 5. 组件详细设计

### 5.1 `SLogRecord`(POD,固定 64B)

```cpp
#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"

struct SLogRecord
{
    // Header — 28B
    uint64    TimestampNs;   // steady_clock 纳秒,单调
    uint32    ThreadId;      // 平台 thread id(仅用于显示)
    uint16    CategoryId;    // MLogRegistry 分配的紧凑 ID
    uint8     Level;         // ELogLevel 强转
    uint8     Flags;         // bit0: MessageOverflowed(>16B),bit1: HasSourceLocation

    // Source location — 12B(索引到全局 string table)
    uint32    FileStringId;
    uint16    Line;
    uint16    FuncStringId;

    // Routing / Context — 8B
    uint32    SinkMask;
    uint32    ContextSnapshotId;

    // Payload — 16B(SSO 内联或外溢标记)
    union
    {
        struct { char Data[16]; } Inline;
        struct { uint32 Offset; uint32 Length; uint32 Capacity; uint32 Reserved; } Overflow;
    } Payload;

    // 合计 64B;编译期 static_assert(sizeof(SLogRecord) == 64)
};
```

**关键决策:**
- `TimestampNs` 用 `steady_clock`(单调,不受系统时间跳变影响);显示时若需要 wall-clock,后台格式化阶段做映射
- `FileStringId` / `FuncStringId` 走 `MLogStringTable`,避免每条记录持字符串指针
- `SinkMask` 在 enqueue 阶段确定,后台不再决策
- 消息分两段:SSO(≤16B)直接存 `Payload.Inline.Data`;超过则分配堆内存(线程局部 TLS 缓冲),`Payload.Overflow` 记录 `[Offset, Length, Capacity]`;消费端通过 `MLogContext` 复位 TLS 缓冲

### 5.2 `TMpscRingBuffer<SLogRecord>`

```cpp
template<typename T>
class TMpscRingBuffer
{
public:
    explicit TMpscRingBuffer(size_t CapacityPow2);

    bool TryEnqueue(const T& Record);
    void BlockingEnqueue(const T& Record);  // 仅 ERROR/CRITICAL,带超时 100ms

    size_t DequeueBatch(T* OutBuffer, size_t MaxCount);

    size_t Capacity() const;
    size_t ApproxSize() const;

private:
    T*                  Slots;
    size_t              CapacityMask;
    std::atomic<size_t> EnqueuePos{0};
    size_t              DequeuePos{0};   // 单消费者独占,无需 atomic
};
```

**关键决策:**
- 容量固定 2 的幂(默认 1<<20 = 1M 条 ~64MB)
- `EnqueuePos` 用 `fetch_add` 抢占 slot;`DequeuePos` 单线程独占
- 写 slot 时 `release` fence,读时 `acquire`

### 5.3 丢弃策略 `EEvictionPolicy`

```cpp
enum class EEvictionPolicy : uint8
{
    DropNewest,    // 满时直接拒绝新条目
    DropOldest,    // 覆盖最早条目,保留 ERROR+ 不被覆盖(默认)
    BlockOnFull,   // 全等级阻塞(不推荐)
};
```

`DropOldest` 实现:推进 `DequeuePos` 时跳过 `[Tail..Tail+32)` 区间内 `Level >= ERROR` 的条目;若 32 条全是 ERROR+,本次 enqueue 失败并 `IncDropped(ProtectedOverflow)`。

### 5.4 `MLogContext` + `MLogContextScope`(TLS)

```cpp
class MLogContext
{
public:
    struct SEntry
    {
        MStringView Key;
        MStringView Value;
    };

    void Set(MStringView Key, MStringView Value);
    void Set(MStringView Key, int64 Value);
    void Set(MStringView Key, uint64 Value);
    void Unset(MStringView Key);

    uint32 CaptureSnapshot();   // Enqueue 时调用
    void   ReleaseSnapshot(uint32 SnapshotId);

    static MLogContext& GetTLS();
};

class MLogContextScope
{
public:
    MLogContextScope(MStringView Key, MStringView Value)
    {
        MLogContext::GetTLS().Set(Key, Value);
        SavedKey = Key;
    }
    ~MLogContextScope() { MLogContext::GetTLS().Unset(SavedKey); }
private:
    MStringView SavedKey;
};
```

**关键决策:**
- 快照池是固定大小循环数组(`kMaxSnapshots = 4096`),`SnapshotId` 是槽位索引
- `ReleaseSnapshot` 由 `MLogDispatcher` 在写完 sink 后调用
- 典型用法:`{ MLogContextScope ActorScope("actor_id", "1001"); NET_LOG(Info, "msg"); }`

### 5.5 `MLogRegistry` + `MLogRouter` + `MLogMetrics`

```cpp
class MLogRegistry
{
public:
    static MLogRegistry& Get();
    const SLogCategory& RegisterOrGet(const char* Name, ELogLevel DefaultLevel, SLogCategory* StaticInstance);
    const SLogCategory& GetById(uint16 Id) const;
    const SLogCategory* FindByName(const MString& Name) const;
    size_t NumCategories() const;
private:
    TVector<SLogCategory> Categories;  // ID = index
};

// 路由表 — 启动期构建,运行时单写多读
struct SLogRouteRule
{
    const SLogCategory* Category;
    uint32 SinkMask;            // bitmask: 1<<0=Console, 1<<1=File, 1<<2=Udp, 1<<3=Tcp, 1<<4=Coredump
    ELogLevel MinLevel;
};

class MLogRouter
{
public:
    static MLogRouter& Get();
    void SetRule(const SLogRouteRule& Rule);  // 拷一份 swap
    void ClearRules();
    uint32 ResolveSinkMask(uint16 CategoryId, ELogLevel Level) const;
private:
    std::atomic<TArray<uint32>*> CategoryToMask{nullptr};
    std::atomic<TArray<ELogLevel>*> CategoryToMinLevel{nullptr};
};

// 计数器
struct SLogMetricsSnapshot
{
    uint64 Enqueued;
    uint64 DroppedEvicted;
    uint64 DroppedOverflow;
    uint64 BlockedEnqueues;
    uint64 DispatchedBatches;
    TArray<uint64> WrittenBytesPerSink;
    TArray<uint64> SuppressedByCategory;
};

class MLogMetrics
{
public:
    static void IncEnqueued();
    static void IncDropped(EEvictionReason Reason);
    static void IncBlockedEnqueues();
    static void AddWrittenBytes(uint8 SinkId, uint64 Bytes);
    static void IncSuppressedByCategory(uint16 CategoryId);
    static SLogMetricsSnapshot Snapshot();
};
```

### 5.6 `MLogDispatcher` 线程

```cpp
class MLogDispatcher
{
public:
    MLogDispatcher(TMpscRingBuffer<SLogRecord>& Queue, TArray<MLogSinkWriter*>& Writers);
    void Start();
    void Stop();  // drain + join
private:
    void RunLoop();
    void DispatchBatch(TSpan<SLogRecord> Batch);
};
```

- 线程命名:`mlog-dispatch`
- 批大小默认 256 条或 64KB,先到先触发
- `DispatchBatch` 按 `(Record.SinkMask)` 分桶;每桶整批投递(Writer 端整批处理)

### 5.7 `MLogSinkWriter` 线程(每 sink 一个)

```cpp
class MLogSinkWriter
{
public:
    MLogSinkWriter(TUniquePtr<ILogSink> Sink, EFlushPolicy Policy);
    void Start();
    void Stop();
    void EnqueueBatch(TArray<SLogRecord>&& Batch);
private:
    void RunLoop();
    void MaybeFlush(bool bForce);
};
```

- 线程命名:`mlog-<sinkname>-writer`
- `EFlushPolicy`:`Interval`(默认 10ms)/ `SizeThreshold`(默认 64KB)/ `IntervalOrSize`(默认)
- `MaybeFlush(bForce=true)` 调 `Sink->Flush()`(对 FileSink 触发 `fflush` + 周期性 `fdatasync`)

### 5.8 Sinks(接口 + 4 个实现)

```cpp
class ILogSink
{
public:
    virtual ~ILogSink() = default;
    virtual bool Open() = 0;
    virtual void Close() = 0;
    virtual void WriteBatch(TSpan<const SLogRecord> Batch, TSpan<char> OutBuffer) = 0;
    virtual void Flush() = 0;
    virtual ELogLevel MinLevel() const = 0;
    virtual const char* Name() const = 0;
};
```

**`MConsoleSink`** — stdout;`WriteBatch` 序列化为文本行(含 Category 前缀如 `[Net][Info]`);`MinLevel` 可调;`bUseColor` 默认 false。
**`MRollingFileSink`** — 按大小滚动(默认 100MB);`WriteBatch` 序列化 JSON Lines 到 `fwrite` 缓冲;`Flush()` 触发 `fflush` + 周期性 `fdatasync`(每 N 次 flush 一次,默认 N=10)。
**`MUdpSink` / `MTcpSink`** — JSON Lines;`MTcpSink` 维护连接 + 重连;`MUdpSink` 批量组装 + 分片。
**`MCoredumpSink`** — 详见 §6。

### 5.9 调用入口(facade)

```cpp
namespace MLog
{
    struct SInitParams
    {
        MString ConfigPath;             // 可选,JSON
        ELogLevel GlobalDefaultLevel;   // 默认 Info
        bool bUseColor;                 // ConsoleSink,默认 false
        MString LogDir;                 // RollingFileSink 目录,默认 "Logs"
        size_t RotatedFileBytes;        // 默认 100MB
        size_t NumArchives;             // 默认 5
        bool bEnableUdp;                // 默认 false
        MString UdpTarget;              // "ip:port"
        bool bEnableTcp;                // 默认 false
        MString TcpTarget;              // "ip:port"
    };

    void Init(const SInitParams& Params);
    void Shutdown();  // drain + stop all threads

    void Write(const SLogCategory& Category, ELogLevel Level, const char* Fmt, ...);
    void WriteV(const SLogCategory& Category, ELogLevel Level, const char* Fmt, va_list Args);
    void HandleFatal(const SLogCategory& Cat, const char* Fmt, va_list Args);
}
```

### 5.10 启动序列

1. `MLogStringTable::Init()` — 收集已通过 `MLOG_FILE` 注册的源路径
2. `MLogRegistry::Get()` 创建,逐个 `RegisterOrGet` 每个 Category
3. 解析 `Params.ConfigPath`;应用 filter + 路由(JSON)
4. 构造 Sinks;`Open()` 失败 → log 到 stderr + 跳过该 sink(不致命)
5. 启动 `MLogSinkWriter` × N + `MLogDispatcher`
6. `MLog::Write` 入口就绪

## 6. Coredump / FATAL 路径

### 6.1 `MCoredumpSink` 同步采集

**职责:** FATAL(`ELogLevel::Critical`)日志触发时,**不走异步队列**,同步完成:

1. 抓取最近 N 条 `SLogRecord`(从 RingBuffer 头部逆向扫描;N 默认 1000)
2. 收集系统信息:进程 PID、命令行、可执行文件路径、`/proc/self/maps`、`/proc/self/status`、uname
3. 收集调用栈:`backtrace()/backtrace_symbols()`(glibc)
4. 序列化为 JSON Lines 写入 `Logs/coredump/<service>-<ts>.jsonl`
5. 调 `MLog::Flush()` —— 阻塞至所有现有 Async 队列排空
6. 触发进程级 dump:`raise(SIGABRT)`

```cpp
class MCoredumpSink : public ILogSink
{
public:
    struct SConfig
    {
        MString DumpDir = "Logs/coredump";
        size_t RecentRecords = 1000;
        bool bForceCoreDump = true;
        TFunction<void(const MString& DumpPath)> UploadHook;  // 预留 Coredump 平台上报
    };

    void HandleFatal(SLogRecord&& TriggeringRecord);

    void OnDumpComplete(const MString& Path);
private:
    SConfig Config;
};
```

### 6.2 Upload hook(预留,本期不实现)

```cpp
MCoredumpSink::SConfig Config;
Config.UploadHook = [](const MString& DumpPath) {
    // TODO(coredump-platform): HTTP POST to collector service
    // 本期不实现,接口预留
};
```

`OnDumpComplete` 触发 hook 时,**不阻塞进程退出**(在 `std::thread` 上异步执行;若上传失败写 stderr,后续不上报)。

### 6.3 与 RingBuffer 的协作

- FATAL 触发时调 `MLog::Flush()`:Dispatcher 把当前队列内数据全部 fan-out 到各 Writer,Writer 全部 flush 到目标
- `MCoredumpSink` 自带 writer,且 `HandleFatal` 直接同步写(不经过 `MLogSinkWriter` 线程)

## 7. 错误处理

| 故障 | 检测 | 处理 |
|---|---|---|
| 磁盘满 | `fwrite` 返回 < 期望字节 | `MLogMetrics::IncDropped(DiskFull)`;不重试 |
| 文件被删/移动 | `fwrite` 返回 0 | 尝试 1 次 reopen;失败则该 sink 暂停 5s 后再试 |
| 网络断开(UDP/TCP) | `sendto`/`send` 返回 EAGAIN/ECONNRESET | UDP:丢并计数;TCP:触发重连,期间丢并计数 |
| Sink 慢/阻塞 | 单 batch > 100ms | `MLogMetrics::AddSlowSink(SinkId, BatchMs)`;不阻塞其他 sink |
| Sink `Open()` 失败 | `Open()` 返回 false | 启动时不致命,记录 stderr,后续 `WriteBatch` no-op |
| 进程退出时 | `MLog::Shutdown()` | 强 drain + 双 timeout(5s/10s);超时则 `std::abort()` |

**关键不变量:** 单 sink 故障**不影响**其它 sink 与业务线程。

## 8. Filter + Routing

### 8.1 Filter

**作用域:Category 维度。** 每条日志入队前过滤:

```
Enqueue 时:
  1. 取 SLogCategory& Cat = Registry.GetById(Record.CategoryId)
  2. if Cat.bSuppressed        → SuppressedByCategory[Cat.Id]++, 不入队
  3. if Record.Level < Cat.RuntimeLevel → 同上
  4. 否则入队
```

**运行时 API:**

```cpp
namespace MLog
{
    void SetCategoryLevel(const SLogCategory& Cat, ELogLevel Level);
    void SetCategorySuppressed(const SLogCategory& Cat, bool bSuppressed);

    struct SCategoryConfig
    {
        MString Name;
        ELogLevel Level;
        bool bSuppressed;
    };
    void ApplyCategoryConfig(const TVector<SCategoryConfig>& Configs);
}
```

**配置文件 JSON(启动期加载):**

```json
{
  "categories": [
    { "name": "Net",  "level": "Debug",   "suppressed": false },
    { "name": "Db",   "level": "Warn",    "suppressed": false },
    { "name": "Auth", "level": "Info",    "suppressed": true  }
  ],
  "routes": [
    { "name": "Auth", "sinks": ["console", "file"], "minLevel": "Info" },
    { "name": "Db",   "sinks": ["console", "file"], "minLevel": "Debug" }
  ]
}
```

加载失败 → `MLog::Init()` 打 ERROR 并回退默认(所有 category 保持 `DECLARE` 时的初始 level,不静默)。

### 8.2 Routing

**作用域:Category → Sink 集合。**

```cpp
struct SLogRouteRule
{
    const SLogCategory* Category;
    uint32 SinkMask;      // 1<<0=Console, 1<<1=File, 1<<2=Udp, 1<<3=Tcp, 1<<4=Coredump
    ELogLevel MinLevel;
};

class MLogRouter
{
public:
    static MLogRouter& Get();
    void SetRule(const SLogRouteRule& Rule);
    void ClearRules();
    uint32 ResolveSinkMask(uint16 CategoryId, ELogLevel Level) const;
private:
    std::atomic<TArray<uint32>*> CategoryToMask{nullptr};
    std::atomic<TArray<ELogLevel>*> CategoryToMinLevel{nullptr};
};
```

**调用点路径(无锁):**

```
1. Record.CategoryId → CategoryToMask[Id] & CategoryToMinLevel[Id]
2. if (SinkMask == 0) 丢弃(走 suppression 计数)
3. if (Level < MinLevel) 丢弃
4. 入队(带 SinkMask)
5. Dispatcher 按 SinkMask fan-out
```

**默认行为:** 规则表为空时,所有 category `SinkMask=0xFFFFFFFF`,`MinLevel=Trace`。

**运行时改路由(动态调):**

```cpp
// Auth 类别只本地,不上远程
MLogRouter::Get().SetRule({ &LogAuth, (1u<<0)|(1u<<1), ELogLevel::Info });
```

## 9. 文件清单

```
Common/Runtime/Log/
├── LogLevel.h                  (扩展: ELogLevel + EEvictionPolicy + EFlushPolicy)
├── LogCategory.h               (新增: SLogCategory + DECLARE/DEFINE 宏)
├── LogCategories.h             (新增: 项目级 Category 总集)
├── LogCategories.cpp           (新增: DEFINE 实例化)
├── LogRecord.h                 (新增: SLogRecord POD)
├── LogContext.h / .cpp         (新增: MLogContext TLS + MLogContextScope)
├── LogStringTable.h / .cpp     (新增: MLogStringTable)
├── MpscRingBuffer.h            (新增: 模板无锁队列,header-only)
├── LogRegistry.h / .cpp        (新增: MLogRegistry)
├── LogRouter.h / .cpp          (新增: MLogRouter)
├── LogFilter.h / .cpp          (新增: MLogFilter)
├── LogMetrics.h / .cpp         (新增: MLogMetrics)
├── LogSinks.h                  (保留: ILogSink 接口)
├── ConsoleSink.h / .cpp        (新增: MConsoleSink)
├── RollingFileSink.h / .cpp    (新增: MRollingFileSink)
├── UdpSink.h / .cpp            (新增: MUdpSink)
├── TcpSink.h / .cpp            (新增: MTcpSink)
├── CoredumpSink.h / .cpp       (新增: MCoredumpSink)
├── SinkWriter.h / .cpp         (新增: MLogSinkWriter 线程)
├── Dispatcher.h / .cpp         (新增: MLogDispatcher 线程)
├── LogConfig.h / .cpp          (新增: 配置结构 + JSON 解析)
└── Log.h                       (新增: MLog facade)
```

**删除:** `Logger.h/.cpp`、`FileLogSink.h/.cpp`、`ConsoleLogSink.h/.cpp`

## 10. Build 集成

- `Source/Common/Runtime/Log/CMakeLists.txt` 新建(若 Common 模块用 glob 则无需)
- 新依赖:nlohmann/json(若 `MLogConfig` 选 JSON 解析)
- 头文件路径:`#include "Common/Runtime/Log/Log.h"`、`#include "Common/Runtime/Log/LogCategories.h"`

## 11. 测试策略

### 11.1 单元测试

| 模块 | 测试重点 |
|---|---|
| `TMpscRingBuffer<T>` | 多生产者并发 enqueue / 单消费者 dequeue;满时策略 |
| `SLogRecord` | `static_assert(sizeof == 64)` |
| `MLogContext` + TLS | 嵌套 scope;snapshot/restore;线程隔离 |
| `MLogRegistry` | RegisterCategory 唯一 Id;运行时改 Level 立即生效 |
| `MLogRouter` | 路由表应用后 SinkMask 正确 |
| `MLogFilter` | 静默 / 级别过滤;计数 |
| `MLogMetrics` | 各计数器累加正确;snapshot 一致性 |
| `MLogStringTable` | 同字符串 intern 返回同 id |
| 各 Sink | `WriteBatch` 后内容正确(JSON 解析 / 文本匹配) |
| `MCoredumpSink` | dump 文件生成 + 行数 |

### 11.2 性能测试

```cpp
TEST(LogPerf, ConcurrentEmit)
{
    MLog::Init({ .LogDir = "/tmp" });
    constexpr int NumThreads = 8;
    constexpr int RecordsPerThread = 200'000;
    // ... 8 线程并发 emit
    // 断言:总 throughput ≥ 1'000'000 rec/sec
}

TEST(LogPerf, P99Latency)
{
    // 单线程 100K 条,记录每条 emit 耗时
    // 目标:p99 < 1µs
}

TEST(LogStress, BackpressureOnFull)
{
    // 极小 RingBuffer + 极慢 Writer
    // 期望:DropOldest 生效,ERROR+ 不被覆盖
}

TEST(LogLifecycle, ShutdownDrains)
{
    // enqueue 10K 条,立即 Shutdown
    // 期望:全部落盘(在 5s 内)
}
```

### 11.3 端到端脚本

- `Scripts/validate_log_basics.py` — 启动任一服务,断言文件存在、行数、Category 标签、JSON 解析有效
- `Scripts/validate_log_rotation.py` — 写满 100MB,断言 rotate 生效、归档数 ≤ NumArchives
- `Scripts/validate_log_fatal.py` — 触发 `LOG_FATAL_EX(LogNet, ...)`,断言 coredump 文件存在 + 含触发 FATAL + 最近 N 条
- `Scripts/validate_log_routing.py` — 配置 Auth 只走 File,断言 Console 无 Auth 行
- `Scripts/validate_log_filter.py` — 配置某 category 静默,断言该 category 无任何 sink 输出 + SuppressedByCategory 计数增加

## 12. 落地步骤

1. **基础设施(零外部依赖)**:LogLevel、MpscRingBuffer、SLogRecord、MLogStringTable、MLogContext
2. **核心机制**:MLogRegistry + DECLARE/DEFINE、MLogRouter、MLogFilter、MLogMetrics
3. **Sink 实现**:ILogSink、MConsoleSink、MRollingFileSink、MUdpSink、MTcpSink、MCoredumpSink
4. **线程与调度**:MLogSinkWriter、MLogDispatcher、MLog::Init/Shutdown
5. **迁移**:删除旧 Logger.h/.cpp、FileLogSink、ConsoleLogSink;在 EchoService 替换;业务调用点 50% 自动迁移 + 50% 升级到具体 Category
6. **验证**:单元测试 + 性能测试 + 端到端脚本

## 13. 风险与缓解

| 风险 | 等级 | 缓解 |
|---|---|---|
| `MLogContext` TLS 与动态库交互 | 中 | 避免在动态库 unload 时持 TLS 引用;`MLog::OnThreadExit()` 钩子 |
| `MCoredumpSink::HandleFatal` 在 SIGABRT handler 前未完成 IO | 中 | `raise(SIGABRT)` 之前调 `fflush+fsync` dump 文件 |
| RingBuffer 满时丢日志导致指标虚低 | 低 | `MLogMetrics::Snapshot()` 暴露 `DroppedEvicted/Overflow` |
| 多 Sink 共享 RingBuffer,Dispatcher 单点变瓶颈 | 低 | batch size 调优 + profiler 验证 |
| 源路径字符串池膨胀 | 低 | 实测 < 5K 条,< 1MB,无需 dedup |
| `BlockingEnqueue` 100ms 超时后仍阻塞 | 中 | FATAL 不调用 `BlockingEnqueue`,走 `MCoredumpSink` 同步路径 |
| 进程退出时 drain 超时 | 低 | 测试覆盖;超时 fallback `std::abort()` |
| 配置 JSON 解析失败 | 低 | 静默回退默认,ERROR 到 stderr,启动不阻塞 |
| `backtrace()` 在 FATAL 路径调时栈已被破坏 | 中 | `MCoredumpSink` 不依赖应用栈;OS 层 coredump 仍会捕完整栈 |

## 14. 不在本期范围

- Coredump 平台上报(`UploadHook` 接口预留,不实现上传)
- HTTP `/metrics` endpoint 暴露 `MLogMetrics`(后续 Metrics 模块)
- 日志聚合 / ELK 集成
- 控制台 ANSI 着色自动检测
- 跨平台 `MCoredumpSink` 的 Windows 实现(本期 Unix 优先)
- 日志采样(Sampling;按概率丢弃低级别)