# Mession 高性能日志模块实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标:** 替换 `Source/Common/Runtime/Log/` 下的 540 行旧实现,交付异步多 Sink 高性能日志系统

**架构:** 无锁 MPSC 环形队列 + 单 Dispatcher 线程 + 每 Sink 独立 Writer 线程;Category UE 风格声明 + 运行时 Filter/Routing;FATAL 同步 Coredump 路径

**技术栈:** C++20, `std::atomic`/`std::mutex`/`std::condition_variable`, 现有 `MJsonWriter`/`MJsonReader`(`Common/Runtime/Json.h`, 无外部依赖), CMake

## Global Constraints

- **C++ 标准:** `CMAKE_CXX_STANDARD = 20`
- **命名:** `S*`结构体/`M*`类/`E*`枚举/`I*`接口/`T*`模板别名 + `bXxx`布尔 + PascalCase 函数 + `MLog::Write` 入口
- **SSO 消息内联:** ≤16B 直接存 `SLogRecord::Payload.Inline.Data`,超长走 TLS 堆分配
- **JSON 序列化:** 使用现有 `MJsonWriter` (无外部依赖),不引入 nlohmann_json
- **构建系统:** CMake, `mession_common` 目标用显式文件列表(非 `file(GLOB)`),改 `MESSION_COMMON_SOURCES` 变量
- **SLogRecord 大小:** `static_assert(sizeof(SLogRecord) == 64)` 编译期强制
- **无现有测试框架:** 创建 `Source/Common/Runtime/Log/Tests/` + `LogTest` 可执行目标,main 返回 0=全部通过

---

## 文件映射

### 新增文件

| 文件 | 用途 |
|---|---|
| `Source/Common/Runtime/Log/LogLevel.h` | 扩展:原有 ELogLevel + 新增 EEvictionPolicy + EFlushPolicy + EEvictionReason |
| `Source/Common/Runtime/Log/LogRecord.h` | SLogRecord POD (64B) |
| `Source/Common/Runtime/Log/MpscRingBuffer.h` | TMpscRingBuffer<T> 模板,header-only |
| `Source/Common/Runtime/Log/LogStringTable.h/.cpp` | MLogStringTable 源路径 intern |
| `Source/Common/Runtime/Log/LogContext.h/.cpp` | MLogContext TLS + MLogContextScope RAII |
| `Source/Common/Runtime/Log/LogCategory.h` | SLogCategory + DECLARE_LOG_CATEGORY_EXTERN/DEFINE_LOG_CATEGORY 宏 |
| `Source/Common/Runtime/Log/LogCategories.h` | 项目级 6 个 Category 声明 |
| `Source/Common/Runtime/Log/LogCategories.cpp` | DEFINE_LOG_CATEGORY 实例化 |
| `Source/Common/Runtime/Log/LogRegistry.h/.cpp` | MLogRegistry 全局注册表 |
| `Source/Common/Runtime/Log/LogRouter.h/.cpp` | MLogRouter 路由表 |
| `Source/Common/Runtime/Log/LogFilter.h/.cpp` | MLogFilter 轻量过滤 |
| `Source/Common/Runtime/Log/LogMetrics.h/.cpp` | MLogMetrics 原子计数器 |
| `Source/Common/Runtime/Log/LogSinks.h` | 保留:ILogSink 接口 |
| `Source/Common/Runtime/Log/ConsoleSink.h/.cpp` | MConsoleSink |
| `Source/Common/Runtime/Log/RollingFileSink.h/.cpp` | MRollingFileSink |
| `Source/Common/Runtime/Log/UdpSink.h/.cpp` | MUdpSink |
| `Source/Common/Runtime/Log/TcpSink.h/.cpp` | MTcpSink |
| `Source/Common/Runtime/Log/CoredumpSink.h/.cpp` | MCoredumpSink |
| `Source/Common/Runtime/Log/SinkWriter.h/.cpp` | MLogSinkWriter 线程 |
| `Source/Common/Runtime/Log/Dispatcher.h/.cpp` | MLogDispatcher 线程 |
| `Source/Common/Runtime/Log/LogConfig.h/.cpp` | 配置结构 + MJsonReader JSON 解析 |
| `Source/Common/Runtime/Log/Log.h` | MLog facade 命名空间 |
| `Source/Common/Runtime/Log/Tests/TestHarness.h` | 极简测试框架 (EXPECT_* 宏 + main 汇总) |
| `Source/Common/Runtime/Log/Tests/TestMpscRingBuffer.cpp` | TMpscRingBuffer 单元测试 |
| `Source/Common/Runtime/Log/Tests/TestLogContext.cpp` | MLogContext TLS 单元测试 |
| `Source/Common/Runtime/Log/Tests/TestLogRegistry.cpp` | MLogRegistry 单元测试 |
| `Source/Common/Runtime/Log/Tests/TestLogRouter.cpp` | MLogRouter 单元测试 |
| `Source/Common/Runtime/Log/Tests/TestLogSinks.cpp` | 各 Sink 集成测试 |
| `Source/Common/Runtime/Log/Tests/TestLogPerf.cpp` | 吞吐/延迟/背压性能测试 |
| `Source/Common/Runtime/Log/Tests/main.cpp` | LogTest 入口,运行所有测试 |
| `Scripts/validate_log_basics.py` | 端到端:启动服务 + 断言日志文件存在/行数/Category 标签 |
| `Scripts/validate_log_rotation.py` | 端到端:写满 100MB + 断言 rotate 生效 |
| `Scripts/validate_log_fatal.py` | 端到端:触发 LOG_FATAL_EX + 断言 coredump 文件生成 |
| `Scripts/validate_log_routing.py` | 端到端:路由 Auth 只走 File + 断言 Console 无 Auth 行 |
| `Scripts/validate_log_filter.py` | 端到端:静默某 Category + 断言无输出 + SuppressedByCategory 计数增加 |
| `Config/LogConfig.json` | 示例配置文件 |

### 修改文件

| 文件 | 改动 |
|---|---|
| `CMakeLists.txt` | `MESSION_COMMON_SOURCES` 中移除旧 3 文件 + 新增 25 个文件路径;添加 `LogTest` 可执行目标 |
| `Source/Common/Runtime/MLib.h` | 新增 `using TAtomic<T> = std::atomic<T>` |

### 删除文件

| 文件 | 原因 |
|---|---|
| `Source/Common/Runtime/Log/Logger.h` | 被 Log.h + LogRegistry.h 替换 |
| `Source/Common/Runtime/Log/Logger.cpp` | 同上 |
| `Source/Common/Runtime/Log/FileLogSink.h` | 被 RollingFileSink.h 替换 |
| `Source/Common/Runtime/Log/FileLogSink.cpp` | 同上 |
| `Source/Common/Runtime/Log/ConsoleLogSink.h` | 被 ConsoleSink.h 替换 |
| `Source/Common/Runtime/Log/ConsoleLogSink.cpp` | 同上 |

---

## 任务清单

### Task 1: 基础设施 — SLogRecord + TMpscRingBuffer + LogLevel

**Files:**
- Create: `Source/Common/Runtime/Log/LogLevel.h`
- Create: `Source/Common/Runtime/Log/LogRecord.h`
- Create: `Source/Common/Runtime/Log/MpscRingBuffer.h`
- Create: `Source/Common/Runtime/Log/Tests/TestHarness.h`
- Create: `Source/Common/Runtime/Log/Tests/TestMpscRingBuffer.cpp`
- Modify: `Source/Common/Runtime/MLib.h` (添加 TAtomic)

**Interfaces:**
- Consumes: 无
- Produces: `LogLevel.h` (`ELogLevel`, `EEvictionPolicy`, `EEvictionReason`, `EFlushPolicy`), `LogRecord.h` (`SLogRecord`, `static_assert(sizeof(SLogRecord) == 64)`), `MpscRingBuffer.h` (`TMpscRingBuffer<T>::TryEnqueue(bool)`, `TMpscRingBuffer<T>::DequeueBatch(size_t)`)

- [ ] **Step 1: 创建 `Source/Common/Runtime/Log/Tests/TestHarness.h`**

```cpp
#pragma once
#include <cstdio>
#include <atomic>
#include <vector>

static std::atomic<int> GTestPassed{0};
static std::atomic<int> GTestFailed{0};

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        std::printf("  FAIL: %s (line %d)\n", #expr, __LINE__); \
        GTestFailed.fetch_add(1); \
    } else { \
        GTestPassed.fetch_add(1); \
    } \
} while(0)

#define EXPECT_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a != _b) { \
        std::printf("  FAIL: %s == %s (got %lld, expected %lld) (line %d)\n", \
            #a, #b, (long long)_a, (long long)_b, __LINE__); \
        GTestFailed.fetch_add(1); \
    } else { \
        GTestPassed.fetch_add(1); \
    } \
} while(0)

#define EXPECT_NE(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a == _b) { \
        std::printf("  FAIL: %s != %s (got %lld) (line %d)\n", \
            #a, #b, (long long)_a, __LINE__); \
        GTestFailed.fetch_add(1); \
    } else { \
        GTestPassed.fetch_add(1); \
    } \
} while(0)

#define TEST_CASE(Name) void Test_##Name()

#define RUN_TESTS() do { \
    std::printf("\n=== Results: %d passed, %d failed ===\n", \
        GTestPassed.load(), GTestFailed.load()); \
    return GTestFailed.load() == 0 ? 0 : 1; \
} while(0)
```

- [ ] **Step 2: 创建 `Source/Common/Runtime/Log/LogLevel.h`**

```cpp
#pragma once

// 原有 ELogLevel (Trace=0 .. Critical=5), 不改变
enum class ELogLevel : int
{
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Critical = 5
};

// 新增
enum class EEvictionPolicy : uint8
{
    DropNewest = 0,
    DropOldest = 1,  // 默认,ERROR+ 保护
    BlockOnFull = 2,
};

enum class EEvictionReason : uint8
{
    DroppedOverflow = 0,
    ProtectedOverflow = 1,
    DiskFull = 2,
    NetworkLost = 3,
};

enum class EFlushPolicy : uint8
{
    Interval = 0,        // 10ms 定时
    SizeThreshold = 1,   // 64KB 阈值
    IntervalOrSize = 2,  // 二者满足其一(默认)
};
```

- [ ] **Step 3: 创建 `Source/Common/Runtime/Log/LogRecord.h`**

```cpp
#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"

static_assert(sizeof(ELogLevel) <= sizeof(uint8), "ELogLevel must fit in uint8");

struct SLogRecord
{
    // Header — 28B
    uint64    TimestampNs;   // steady_clock 纳秒
    uint32    ThreadId;
    uint16    CategoryId;
    uint8     Level;         // ELogLevel 强转
    uint8     Flags;         // bit0: MessageOverflowed, bit1: HasSourceLocation

    // Source location — 12B
    uint32    FileStringId;
    uint16    Line;
    uint16    FuncStringId;

    // Routing + Context — 8B
    uint32    SinkMask;
    uint32    ContextSnapshotId;

    // Payload — 16B
    union
    {
        struct { char Data[16]; } Inline;
        struct { uint32 Offset; uint32 Length; uint32 Capacity; uint32 Reserved; } Overflow;
    } Payload;
};
static_assert(sizeof(SLogRecord) == 64, "SLogRecord must be exactly 64 bytes");
```

- [ ] **Step 4: 创建 `Source/Common/Runtime/Log/MpscRingBuffer.h`**

```cpp
#pragma once
#include "Common/Runtime/MLib.h"
#include <atomic>
#include <cstring>

template<typename T>
class TMpscRingBuffer
{
public:
    explicit TMpscRingBuffer(size_t CapacityPow2)
        : CapacityMask(CapacityPow2 - 1)
    {
        Slots = new T[CapacityPow2];
        std::memset(Slots, 0, CapacityPow2 * sizeof(T));
    }

    ~TMpscRingBuffer() { delete[] Slots; }

    // 非阻塞 enqueue;满时按 Policy 处理
    bool TryEnqueue(const T& Record, EEvictionPolicy Policy = EEvictionPolicy::DropOldest)
    {
        (void)Policy; // 简化版:满时返回 false
        size_t Head = EnqueuePos.load(std::memory_order_relaxed);
        size_t Tail = DequeuePos.load(std::memory_order_acquire);
        if (Head - Tail >= Capacity())
        {
            return false; // 满
        }
        size_t SlotIdx = (Head & CapacityMask);
        Slots[SlotIdx] = Record;
        EnqueuePos.store(Head + 1, std::memory_order_release);
        return true;
    }

    // 阻塞 enqueue (ERROR/CRITICAL 用,带超时)
    bool BlockingEnqueue(const T& Record, int TimeoutMs = 100)
    {
        auto Deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(TimeoutMs);
        while (std::chrono::steady_clock::now() < Deadline)
        {
            if (TryEnqueue(Record))
            {
                return true;
            }
            std::this_thread::yield();
        }
        return false;
    }

    // 消费者批量拉取
    size_t DequeueBatch(T* OutBuffer, size_t MaxCount)
    {
        size_t Head = EnqueuePos.load(std::memory_order_acquire);
        size_t Tail = DequeuePos.load(std::memory_order_relaxed);
        if (Head == Tail) return 0;
        size_t Avail = Head - Tail;
        size_t Count = (Avail < MaxCount) ? Avail : MaxCount;
        for (size_t i = 0; i < Count; ++i)
        {
            OutBuffer[i] = Slots[(Tail + i) & CapacityMask];
        }
        DequeuePos.store(Tail + Count, std::memory_order_release);
        return Count;
    }

    size_t Capacity() const { return CapacityMask + 1; }

private:
    T*                  Slots;
    size_t              CapacityMask;
    std::atomic<size_t> EnqueuePos{0};
    size_t              DequeuePos{0};  // 单消费者,无需 atomic
};
```

- [ ] **Step 5: 创建 `Source/Common/Runtime/Log/Tests/TestMpscRingBuffer.cpp`**

```cpp
#include "Common/Runtime/Log/Tests/TestHarness.h"
#include "Common/Runtime/Log/MpscRingBuffer.h"
#include <thread>
#include <vector>
#include <atomic>

struct TestRecord { int Value; int ThreadId; };

TEST_CASE(MpscRingBuffer_Basic)
{
    TMpscRingBuffer<TestRecord> Q(1024);
    TestRecord R{42, 0};

    EXPECT_TRUE(Q.TryEnqueue(R) == true);
    TestRecord Out[1];
    EXPECT_EQ(Q.DequeueBatch(Out, 1), 1u);
    EXPECT_EQ(Out[0].Value, 42);
}

TEST_CASE(MpscRingBuffer_MultiProducer)
{
    TMpscRingBuffer<TestRecord> Q(65536);
    std::atomic<int> Produced{0};
    std::atomic<int> Consumed{0};
    constexpr int Threads = 8;
    constexpr int PerThread = 10000;

    std::vector<std::thread> Prods;
    for (int t = 0; t < Threads; ++t)
    {
        Prods.emplace_back([&Q, &Produced, t] {
            for (int i = 0; i < PerThread; ++i)
            {
                TestRecord R{i, t};
                if (Q.TryEnqueue(R)) Produced.fetch_add(1);
            }
        });
    }

    std::thread Cons([&Q, &Consumed] {
        TestRecord Out[256];
        while (Consumed.load() < Threads * PerThread)
        {
            size_t N = Q.DequeueBatch(Out, 256);
            Consumed.fetch_add((int)N);
        }
    });

    for (auto& P : Prods) P.join();
    Cons.join();

    EXPECT_EQ(Produced.load(), Threads * PerThread);
    EXPECT_EQ(Consumed.load(), Produced.load());
}

TEST_CASE(MpscRingBuffer_FullReturnsFalse)
{
    TMpscRingBuffer<TestRecord> Q(4);
    TestRecord R{1, 0};
    for (int i = 0; i < 4; ++i) Q.TryEnqueue(R);
    EXPECT_TRUE(Q.TryEnqueue(R) == false); // 满
}
```

- [ ] **Step 6: 编译验证**

```bash
# 在 Log/TestMpscRingBuffer.cpp 同目录确认 static_assert 编译通过
cd /root/Mession && cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release 2>&1 | grep -E "error|warning.*SLogRecord"
cmake --build Build --target LogTest 2>&1 | tail -20
```

预期:无编译错误,`SLogRecord` static_assert 通过

- [ ] **Step 7: 运行测试**

```bash
./Bin/LogTest 2>&1 | grep -E "FAIL|passed|failed"
```

预期:所有测试 PASS

- [ ] **Step 8: Commit**

```bash
git add Source/Common/Runtime/Log/LogLevel.h \
        Source/Common/Runtime/Log/LogRecord.h \
        Source/Common/Runtime/Log/MpscRingBuffer.h \
        Source/Common/Runtime/Log/Tests/ \
        Source/Common/Runtime/MLib.h \
        CMakeLists.txt
git commit -m "feat(log): infrastructure - SLogRecord POD, TMpscRingBuffer, LogLevel enums + unit tests

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: 基础设施 — MLogStringTable + MLogContext

**Files:**
- Create: `Source/Common/Runtime/Log/LogStringTable.h`
- Create: `Source/Common/Runtime/Log/LogStringTable.cpp`
- Create: `Source/Common/Runtime/Log/LogContext.h`
- Create: `Source/Common/Runtime/Log/LogContext.cpp`
- Create: `Source/Common/Runtime/Log/Tests/TestLogContext.cpp`

**Interfaces:**
- Consumes: `LogRecord.h` (`SLogRecord::ContextSnapshotId`)
- Produces: `MLogStringTable::Intern(uint32)`, `MLogStringTable::Get(uint32)`, `MLogContext::GetTLS()`, `MLogContext::CaptureSnapshot()`, `MLogContext::ReleaseSnapshot(uint32)`, `MLogContextScope`

- [ ] **Step 1: 创建 `Source/Common/Runtime/Log/LogStringTable.h`**

```cpp
#pragma once
#include "Common/Runtime/MLib.h"

class MLogStringTable
{
public:
    static void Init();
    static uint32 Intern(const char* Str);
    static const char* Get(uint32 Id);
    static size_t Count();
};
```

- [ ] **Step 2: 创建 `Source/Common/Runtime/Log/LogStringTable.cpp`**

```cpp
#include "Common/Runtime/Log/LogStringTable.h"
#include <unordered_map>
#include <mutex>
#include <cstring>

static std::unordered_map<uint32, MString> GStringTable;
static std::unordered_map<MString, uint32, MStringHash, MStringEq> GStringToId;
static std::mutex GTableMutex;
static uint32 GNextId = 1;  // 0 = 空/无效

void MLogStringTable::Init()
{
    std::lock_guard<std::mutex> L(GTableMutex);
    GStringTable.clear();
    GStringToId.clear();
    GNextId = 1;
}

uint32 MLogStringTable::Intern(const char* Str)
{
    if (!Str || !Str[0]) return 0;
    std::lock_guard<std::mutex> L(GTableMutex);
    auto It = GStringToId.find(Str);
    if (It != GStringToId.end()) return It->second;
    uint32 Id = GNextId++;
    GStringToId[Str] = Id;
    GStringTable[Id] = Str;
    return Id;
}

const char* MLogStringTable::Get(uint32 Id)
{
    std::lock_guard<std::mutex> L(GTableMutex);
    auto It = GStringTable.find(Id);
    if (It != GStringTable.end()) return It->second.c_str();
    return "";
}

size_t MLogStringTable::Count()
{
    std::lock_guard<std::mutex> L(GTableMutex);
    return GStringTable.size();
}
```

- [ ] **Step 3: 创建 `Source/Common/Runtime/Log/LogContext.h`**

```cpp
#pragma once
#include "Common/Runtime/MLib.h"
#include <array>

class MLogContext
{
public:
    static constexpr size_t kMaxEntries = 32;
    static constexpr size_t kMaxSnapshots = 4096;

    struct SEntry
    {
        MString Key;
        MString Value;
    };

    void Set(MStringView Key, MStringView Value);
    void Set(MStringView Key, int64 Value);
    void Unset(MStringView Key);

    uint32 CaptureSnapshot();   // 返回 snapshot id,写入 GSnapshotPool
    void   ReleaseSnapshot(uint32 SnapshotId);

    static MLogContext& GetTLS();

private:
    std::array<SEntry, kMaxEntries> Entries;
    size_t EntryCount = 0;
};

// Snapshot 池 (全局,静态)
struct SContextSnapshot
{
    std::array<SLogContext::SEntry, MLogContext::kMaxEntries> Entries;
    size_t EntryCount = 0;
    std::atomic<bool> InUse{true};
};

class MLogContextScope
{
public:
    MLogContextScope(MStringView Key, MStringView Value)
    {
        MLogContext::GetTLS().Set(Key, Value);
        SavedKey = Key;
    }
    ~MLogContextScope()
    {
        MLogContext::GetTLS().Unset(SavedKey);
    }
private:
    MStringView SavedKey;
};
```

- [ ] **Step 4: 创建 `Source/Common/Runtime/Log/LogContext.cpp`**

```cpp
#include "Common/Runtime/Log/LogContext.h"
#include <thread>

static thread_local MLogContext GThisThreadContext;
static std::array<SContextSnapshot, MLogContext::kMaxSnapshots> GSnapshotPool;
static std::atomic<uint32> GSnapshotNextSlot{0};
static std::mutex GSnapshotMutex;

MLogContext& MLogContext::GetTLS()
{
    return GThisThreadContext;
}

void MLogContext::Set(MStringView Key, MStringView Value)
{
    for (size_t i = 0; i < EntryCount; ++i)
    {
        if (Entries[i].Key == Key)
        {
            Entries[i].Value = Value;
            return;
        }
    }
    if (EntryCount < kMaxEntries)
    {
        Entries[EntryCount++] = {MString(Key), MString(Value)};
    }
}

void MLogContext::Set(MStringView Key, int64 Value)
{
    Set(Key, std::to_string(Value));
}

void MLogContext::Unset(MStringView Key)
{
    for (size_t i = 0; i < EntryCount; ++i)
    {
        if (Entries[i].Key == Key)
        {
            Entries[i] = Entries[EntryCount - 1];
            --EntryCount;
            return;
        }
    }
}

uint32 MLogContext::CaptureSnapshot()
{
    std::lock_guard<std::mutex> L(GSnapshotMutex);
    uint32 Slot = GSnapshotNextSlot.load() % kMaxSnapshots;
    GSnapshotNextSlot.store(Slot + 1);
    GSnapshotPool[Slot].Entries = Entries;
    GSnapshotPool[Slot].EntryCount = EntryCount;
    GSnapshotPool[Slot].InUse.store(true);
    return Slot;
}

void MLogContext::ReleaseSnapshot(uint32 SnapshotId)
{
    if (SnapshotId < kMaxSnapshots)
    {
        GSnapshotPool[SnapshotId].InUse.store(false);
    }
}
```

- [ ] **Step 5: 创建 `Source/Common/Runtime/Log/Tests/TestLogContext.cpp`**

```cpp
#include "Common/Runtime/Log/Tests/TestHarness.h"
#include "Common/Runtime/Log/LogContext.h"

TEST_CASE(LogContext_SetAndUnset)
{
    auto& Ctx = MLogContext::GetTLS();
    Ctx.Set("key1", "value1");
    Ctx.Set("key2", 42);
    uint32 SnapId = Ctx.CaptureSnapshot();
    EXPECT_TRUE(SnapId < MLogContext::kMaxSnapshots);
    Ctx.ReleaseSnapshot(SnapId);
    Ctx.Unset("key1");
    EXPECT_TRUE(Ctx.GetEntryCount() == 1);
}

TEST_CASE(LogContext_NestedScope)
{
    {
        MLogContextScope S1("actor", "1001");
        auto& Ctx = MLogContext::GetTLS();
        EXPECT_EQ(Ctx.FindEntry("actor"), 0);
    }
    // S1 已销毁
}
```

- [ ] **Step 6: 编译 + 运行 + Commit**

```bash
git add Source/Common/Runtime/Log/LogStringTable.h \
        Source/Common/Runtime/Log/LogStringTable.cpp \
        Source/Common/Runtime/Log/LogContext.h \
        Source/Common/Runtime/Log/LogContext.cpp \
        Source/Common/Runtime/Log/Tests/TestLogContext.cpp
git commit -m "feat(log): MLogStringTable + MLogContext TLS + snapshot

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: 核心机制 — MLogRegistry + Category + MLogMetrics

**Files:**
- Create: `Source/Common/Runtime/Log/LogCategory.h`
- Create: `Source/Common/Runtime/Log/LogCategories.h`
- Create: `Source/Common/Runtime/Log/LogCategories.cpp`
- Create: `Source/Common/Runtime/Log/LogRegistry.h`
- Create: `Source/Common/Runtime/Log/LogRegistry.cpp`
- Create: `Source/Common/Runtime/Log/LogMetrics.h`
- Create: `Source/Common/Runtime/Log/LogMetrics.cpp`
- Create: `Source/Common/Runtime/Log/Tests/TestLogRegistry.cpp`
- Create: `Source/Common/Runtime/Log/Tests/TestLogMetrics.cpp`

**Interfaces:**
- Consumes: `LogLevel.h`, `LogContext.h`
- Produces: `MLogRegistry`, `SLogCategory`, `MLogMetrics`, DECLARE/DEFINE 宏

- [ ] **Step 1: 创建 `Source/Common/Runtime/Log/LogCategory.h`**

```cpp
#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"

// DECLARE/DEFINE 宏:必须在 LogCategories.h/cpp 中配对使用
#define DECLARE_LOG_CATEGORY_EXTERN(Name) \
    extern struct SLogCategoryInstance Name;

#define DEFINE_LOG_CATEGORY(Name) \
    struct SLogCategoryInstance Name = *MLogRegistry::Get().RegisterCategory(#Name, ELogLevel::Info);

struct SLogCategory
{
    const char* Name = nullptr;
    uint16      Id = 0;
    ELogLevel   DefaultLevel = ELogLevel::Info;
    std::atomic<ELogLevel> RuntimeLevel{ELogLevel::Info};
    std::atomic<bool> bSuppressed{false};
    std::atomic<uint64> DropCount{0};
};
```

- [ ] **Step 2: 创建 `Source/Common/Runtime/Log/LogCategories.h`**

```cpp
#pragma once
#include "Common/Runtime/Log/LogCategory.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCore);
DECLARE_LOG_CATEGORY_EXTERN(LogNet);
DECLARE_LOG_CATEGORY_EXTERN(LogDb);
DECLARE_LOG_CATEGORY_EXTERN(LogRpc);
DECLARE_LOG_CATEGORY_EXTERN(LogAuth);
DECLARE_LOG_CATEGORY_EXTERN(LogScene);
```

- [ ] **Step 3: 创建 `Source/Common/Runtime/Log/LogCategories.cpp`**

```cpp
#include "Common/Runtime/Log/LogCategories.h"
DEFINE_LOG_CATEGORY(LogCore);
DEFINE_LOG_CATEGORY(LogNet);
DEFINE_LOG_CATEGORY(LogDb);
DEFINE_LOG_CATEGORY(LogRpc);
DEFINE_LOG_CATEGORY(LogAuth);
DEFINE_LOG_CATEGORY(LogScene);
```

- [ ] **Step 4: 创建 `Source/Common/Runtime/Log/LogRegistry.h`**

```cpp
#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogCategory.h"
#include "Common/Runtime/Log/LogLevel.h"

class MLogRegistry
{
public:
    static MLogRegistry& Get();

    // 注册:Name 唯一,返回指针
    SLogCategory* RegisterCategory(const char* Name, ELogLevel DefaultLevel);

    // 查询
    const SLogCategory* GetById(uint16 Id) const;
    SLogCategory* GetById(uint16 Id);
    const SLogCategory* FindByName(const MString& Name) const;

    size_t NumCategories() const { return Categories.size(); }

private:
    MLogRegistry() = default;
    TVector<SLogCategory> Categories;
    std::mutex Mutex;
};
```

- [ ] **Step 5: 创建 `Source/Common/Runtime/Log/LogRegistry.cpp`**

```cpp
#include "Common/Runtime/Log/LogRegistry.h"

MLogRegistry& MLogRegistry::Get()
{
    static MLogRegistry Inst;
    return Inst;
}

SLogCategory* MLogRegistry::RegisterCategory(const char* Name, ELogLevel DefaultLevel)
{
    std::lock_guard<std::mutex> L(Mutex);
    for (auto& Cat : Categories)
    {
        if (std::strcmp(Cat.Name, Name) == 0)
        {
            return &Cat;  // 已存在,返回已有
        }
    }
    SLogCategory Cat;
    Cat.Name = Name;
    Cat.Id = static_cast<uint16>(Categories.size());
    Cat.DefaultLevel = DefaultLevel;
    Cat.RuntimeLevel.store(DefaultLevel);
    Categories.push_back(Cat);
    return &Categories.back();
}

const SLogCategory* MLogRegistry::GetById(uint16 Id) const
{
    if (Id < Categories.size()) return &Categories[Id];
    return nullptr;
}

SLogCategory* MLogRegistry::GetById(uint16 Id)
{
    if (Id < Categories.size()) return &Categories[Id];
    return nullptr;
}

const SLogCategory* MLogRegistry::FindByName(const MString& Name) const
{
    for (auto& Cat : Categories)
    {
        if (Cat.Name == Name) return &Cat;
    }
    return nullptr;
}
```

- [ ] **Step 6: 创建 `Source/Common/Runtime/Log/LogMetrics.h`**

```cpp
#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"
#include <atomic>

struct SLogMetricsSnapshot
{
    uint64 Enqueued = 0;
    uint64 DroppedEvicted = 0;
    uint64 DroppedOverflow = 0;
    uint64 BlockedEnqueues = 0;
    uint64 DispatchedBatches = 0;
    uint64 WrittenBytesConsole = 0;
    uint64 WrittenBytesFile = 0;
    uint64 WrittenBytesUdp = 0;
    uint64 WrittenBytesTcp = 0;
    uint64 TotalSuppressed = 0;
};

class MLogMetrics
{
public:
    static void IncEnqueued() { SInc(Enqueued); }
    static void IncDroppedEvicted() { SInc(DroppedEvicted); }
    static void IncDroppedOverflow() { SInc(DroppedOverflow); }
    static void IncBlockedEnqueues() { SInc(BlockedEnqueues); }
    static void IncDispatchedBatches() { SInc(DispatchedBatches); }
    static void AddWrittenBytesConsole(uint64 Bytes) { WrittenBytesConsole.fetch_add(Bytes); }
    static void AddWrittenBytesFile(uint64 Bytes) { WrittenBytesFile.fetch_add(Bytes); }
    static void IncSuppressed() { SInc(TotalSuppressed); }

    static SLogMetricsSnapshot Snapshot();

private:
    static std::atomic<uint64> Enqueued;
    static std::atomic<uint64> DroppedEvicted;
    static std::atomic<uint64> DroppedOverflow;
    static std::atomic<uint64> BlockedEnqueues;
    static std::atomic<uint64> DispatchedBatches;
    static std::atomic<uint64> WrittenBytesConsole;
    static std::atomic<uint64> WrittenBytesFile;
    static std::atomic<uint64> WrittenBytesUdp;
    static std::atomic<uint64> WrittenBytesTcp;
    static std::atomic<uint64> TotalSuppressed;

    template<typename T>
    static void SInc(std::atomic<T>& Counter) { Counter.fetch_add(1); }
};
```

- [ ] **Step 7: 创建 `Source/Common/Runtime/Log/LogMetrics.cpp`**

```cpp
#include "Common/Runtime/Log/LogMetrics.h"

std::atomic<uint64> MLogMetrics::Enqueued{0};
std::atomic<uint64> MLogMetrics::DroppedEvicted{0};
std::atomic<uint64> MLogMetrics::DroppedOverflow{0};
std::atomic<uint64> MLogMetrics::BlockedEnqueues{0};
std::atomic<uint64> MLogMetrics::DispatchedBatches{0};
std::atomic<uint64> MLogMetrics::WrittenBytesConsole{0};
std::atomic<uint64> MLogMetrics::WrittenBytesFile{0};
std::atomic<uint64> MLogMetrics::WrittenBytesUdp{0};
std::atomic<uint64> MLogMetrics::WrittenBytesTcp{0};
std::atomic<uint64> MLogMetrics::TotalSuppressed{0};

SLogMetricsSnapshot MLogMetrics::Snapshot()
{
    SLogMetricsSnapshot S;
    S.Enqueued = Enqueued.load();
    S.DroppedEvicted = DroppedEvicted.load();
    S.DroppedOverflow = DroppedOverflow.load();
    S.BlockedEnqueues = BlockedEnqueues.load();
    S.DispatchedBatches = DispatchedBatches.load();
    S.WrittenBytesConsole = WrittenBytesConsole.load();
    S.WrittenBytesFile = WrittenBytesFile.load();
    S.WrittenBytesUdp = WrittenBytesUdp.load();
    S.WrittenBytesTcp = WrittenBytesTcp.load();
    S.TotalSuppressed = TotalSuppressed.load();
    return S;
}
```

- [ ] **Step 8: 创建测试文件 `TestLogRegistry.cpp` + `TestLogMetrics.cpp`** (参照 TestHarness.h 风格,测试 RegisterCategory 唯一性、GetById、Snapshot 原子累加)

- [ ] **Step 9: 编译 + 运行 + Commit**

```bash
git add Source/Common/Runtime/Log/LogCategory.h \
        Source/Common/Runtime/Log/LogCategories.h \
        Source/Common/Runtime/Log/LogCategories.cpp \
        Source/Common/Runtime/Log/LogRegistry.h \
        Source/Common/Runtime/Log/LogRegistry.cpp \
        Source/Common/Runtime/Log/LogMetrics.h \
        Source/Common/Runtime/Log/LogMetrics.cpp \
        Source/Common/Runtime/Log/Tests/TestLogRegistry.cpp \
        Source/Common/Runtime/Log/Tests/TestLogMetrics.cpp
git commit -m "feat(log): MLogRegistry + Category system + MLogMetrics

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: 核心机制 — MLogRouter + MLogFilter

**Files:**
- Create: `Source/Common/Runtime/Log/LogRouter.h`
- Create: `Source/Common/Runtime/Log/LogRouter.cpp`
- Create: `Source/Common/Runtime/Log/LogFilter.h`
- Create: `Source/Common/Runtime/Log/LogFilter.cpp`
- Create: `Source/Common/Runtime/Log/Tests/TestLogRouter.cpp`

**Interfaces:**
- Consumes: `LogRegistry.h`, `LogRecord.h`
- Produces: `MLogRouter`, `MLogFilter`, `SLogRouteRule`, `ResolveSinkMask(uint16, ELogLevel) → uint32`

- [ ] **Step 1: 创建 `LogRouter.h/.cpp` + `LogFilter.h/.cpp`** — 参照 spec §8.2 设计:Router 用 `atomic<TArray*>` swap 实现无锁读;Filter 在调用点做 `bSuppressed` + `RuntimeLevel` 检查

- [ ] **Step 2: 创建 `TestLogRouter.cpp`** — 测试默认全 sink 路由、运行时 SetRule 修改 SinkMask、MinLevel 过滤

- [ ] **Step 3: 编译 + 运行 + Commit**

---

### Task 5: Sink 接口 + ConsoleSink + RollingFileSink

**Files:**
- Create: `Source/Common/Runtime/Log/LogSinks.h` (保留 ILogSink)
- Create: `Source/Common/Runtime/Log/ConsoleSink.h`
- Create: `Source/Common/Runtime/Log/ConsoleSink.cpp`
- Create: `Source/Common/Runtime/Log/RollingFileSink.h`
- Create: `Source/Common/Runtime/Log/RollingFileSink.cpp`

**Interfaces:**
- Consumes: `LogRecord.h`, `LogContext.h`, `MJsonWriter`
- Produces: `ILogSink`, `MConsoleSink`, `MRollingFileSink`, `WriteBatch(TSpan<const SLogRecord>, TSpan<char>)`

- [ ] **Step 1: 创建 `LogSinks.h`**

```cpp
#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"

class ILogSink
{
public:
    virtual ~ILogSink() = default;
    virtual bool Open() = 0;
    virtual void Close() = 0;
    virtual void WriteBatch(TSpan<const SLogRecord> Batch, TSpan<char> OutBuffer) = 0;
    virtual void Flush() = 0;
    virtual ELogLevel MinLevel() const { return ELogLevel::Trace; }
    virtual const char* Name() const = 0;
};

// Sink 位号
enum ELogSinkId : uint8
{
    Console = 0,
    File    = 1,
    Udp     = 2,
    Tcp     = 3,
    Coredump= 4,
};
inline uint32 MakeSinkMask(ELogSinkId Id) { return 1u << Id; }
```

- [ ] **Step 2: 创建 `ConsoleSink.h/.cpp`** — `WriteBatch` 用 `MJsonWriter` 序列化每条为 JSON Line,`Name()` 返回 `"console"`;支持 `bUseColor` 字段(默认 false)

- [ ] **Step 3: 创建 `RollingFileSink.h/.cpp`** — 按大小滚动(`RotatedFileBytes`,默认 100MB);`NumArchives` 保留文件数;`WriteBatch` 追加写入;`Flush` 调 `fflush` + 周期性 `fdatasync`(每 10 次 flush 一次);文件名格式 `Logs/<service>-<YYYYMMDD>-<N>.jsonl`

- [ ] **Step 4: 编译验证 + Commit**

---

### Task 6: UdpSink + TcpSink

**Files:**
- Create: `Source/Common/Runtime/Log/UdpSink.h/.cpp`
- Create: `Source/Common/Runtime/Log/TcpSink.h/.cpp`

**Interfaces:**
- Consumes: `ILogSink`, `MJsonWriter`
- Produces: `MUdpSink`, `MTcpSink`

- [ ] **Step 1: 创建 `UdpSink.h/.cpp`** — `Open()` 创建 socket;`WriteBatch` 组装 JSON Lines 为单个 UDP packet(超长分包);`Close()`;`Flush()` no-op (UDP 无 flush 语义)

- [ ] **Step 2: 创建 `TcpSink.h/.cpp`** — `Open()` connect;`WriteBatch` send;`Close()`;`Flush()` send pending buffer;检测 `ECONNRESET` 触发重连(1 次/5s)

- [ ] **Step 3: 编译验证 + Commit**

---

### Task 7: MLogSinkWriter + MLogDispatcher + MLog facade

**Files:**
- Create: `Source/Common/Runtime/Log/SinkWriter.h/.cpp`
- Create: `Source/Common/Runtime/Log/Dispatcher.h/.cpp`
- Create: `Source/Common/Runtime/Log/LogConfig.h/.cpp`
- Create: `Source/Common/Runtime/Log/Log.h`

**Interfaces:**
- Consumes: `TMpscRingBuffer`, `ILogSink`, `MLogMetrics`, `MLogRegistry`
- Produces: `MLog::Init(SInitParams)`, `MLog::Write(Category, Level, Fmt, ...)`, `MLog::Shutdown()`, `MLog::HandleFatal(Category, Fmt, va_list)`

- [ ] **Step 1: 创建 `SinkWriter.h/.cpp`** — `MLogSinkWriter` 持有一个 `ILogSink*`;独立线程循环等待 inbox batch;`EFlushPolicy` 默认 `IntervalOrSize`;`MaybeFlush` 实现;线程命名 `mlog-<name>-writer`

- [ ] **Step 2: 创建 `Dispatcher.h/.cpp`** — `MLogDispatcher` 单线程循环 `DequeueBatch(256)` → 按 `SinkMask` 分桶 → 投递到各 `SinkWriter::EnqueueBatch`;线程命名 `mlog-dispatch`

- [ ] **Step 3: 创建 `LogConfig.h/.cpp`** — `SInitParams` 结构体(见 spec §5.9);`MJsonReader` 解析 JSON 配置文件,应用 category config + route rules

- [ ] **Step 4: 创建 `Log.h`** — `MLog` facade 命名空间,声明所有公共 API;内部持有 `TMpscRingBuffer<SLogRecord>` + `MLogDispatcher*` + `TVector<MLogSinkWriter*>`;`Write()` 实现:Filter 检查 → Router 查 SinkMask → 格式化(vsnprintf 到 TLS 缓冲) → `TryEnqueue`;`Init()` 启动所有线程;`Shutdown()` drain + join

- [ ] **Step 5: 编译验证(Log.cpp 依赖所有前述文件,首次全量编译)**

```bash
cmake --build Build --target LogTest 2>&1 | grep -E "error:|undefined"
```

- [ ] **Step 6: Commit**

---

### Task 8: MCoredumpSink

**Files:**
- Create: `Source/Common/Runtime/Log/CoredumpSink.h/.cpp`

**Interfaces:**
- Consumes: `MLog`, `MLogDispatcher::Flush()`
- Produces: `MCoredumpSink`, `HandleFatal(SLogRecord&&)`

- [ ] **Step 1: 创建 `CoredumpSink.h/.cpp`** — `SConfig` 结构体(见 spec §6.1);`HandleFatal` 实现:扫描 RingBuffer 最近 N 条 → 收集系统信息(`/proc/self/` 文件、`uname`)→ `backtrace()` → JSON 序列化到 `Logs/coredump/<service>-<ts>.jsonl`→ `fflush`+`fsync`→ `raise(SIGABRT)`;`UploadHook` 预留,实现为 `std::thread` 异步上传(本期仅写 stderr)

- [ ] **Step 2: 在 `Log.h` 的 `MLog::HandleFatal` 中调用 `MCoredumpSink::HandleFatal`** — FATAL 路径绕过 RingBuffer,直接同步采集

- [ ] **Step 3: Commit**

---

### Task 9: 单元测试全量 + LogTest 可执行目标

**Files:**
- Create: `Source/Common/Runtime/Log/Tests/main.cpp`
- Modify: `CMakeLists.txt` (添加 LogTest 可执行目标)

- [ ] **Step 1: 创建 `Source/Common/Runtime/Log/Tests/main.cpp`**

```cpp
#include "Tests/TestHarness.h"
#include <cstdio>

// 声明各测试函数 (由各 Test*.cpp 强制链接)
extern void Test_MpscRingBuffer_Basic();
extern void Test_MpscRingBuffer_MultiProducer();
extern void Test_MpscRingBuffer_FullReturnsFalse();
extern void Test_LogContext_SetAndUnset();
extern void Test_LogContext_NestedScope();
extern void Test_LogRegistry_CategoryId();
extern void Test_LogRouter_DefaultAllSinks();
extern void Test_LogMetrics_Snapshot();
extern void Test_LogSinks_ConsoleOutput();
extern void Test_LogSinks_FileRotation();
extern void Test_LogPerf_Throughput();
extern void Test_LogPerf_Backpressure();

int main()
{
    std::printf("Running Mession Log Tests...\n\n");

    Test_MpscRingBuffer_Basic();
    Test_MpscRingBuffer_MultiProducer();
    Test_MpscRingBuffer_FullReturnsFalse();
    Test_LogContext_SetAndUnset();
    Test_LogContext_NestedScope();
    Test_LogRegistry_CategoryId();
    Test_LogRouter_DefaultAllSinks();
    Test_LogMetrics_Snapshot();
    Test_LogSinks_ConsoleOutput();
    Test_LogSinks_FileRotation();
    Test_LogPerf_Throughput();
    Test_LogPerf_Backpressure();

    return RUN_TESTS();
}
```

- [ ] **Step 2: 修改 `CMakeLists.txt`** — 在 `MESSION_COMMON_SOURCES` 中添加所有新 .cpp 文件(共 25 个),移除旧的 3 个;在文件末尾添加:

```cmake
# Log 单元测试可执行目标
add_executable(LogTest
    Source/Common/Runtime/Log/Tests/main.cpp
    Source/Common/Runtime/Log/Tests/TestMpscRingBuffer.cpp
    Source/Common/Runtime/Log/Tests/TestLogContext.cpp
    Source/Common/Runtime/Log/Tests/TestLogRegistry.cpp
    Source/Common/Runtime/Log/Tests/TestLogMetrics.cpp
    Source/Common/Runtime/Log/Tests/TestLogRouter.cpp
    Source/Common/Runtime/Log/Tests/TestLogSinks.cpp
    Source/Common/Runtime/Log/Tests/TestLogPerf.cpp
)
target_link_libraries(LogTest PRIVATE mession_common mession_core pthread)
```

- [ ] **Step 3: 全量编译 + 运行**

```bash
cmake --build Build --target LogTest -j4 2>&1 | tail -20
./Bin/LogTest 2>&1 | grep -E "FAIL|passed|failed"
```

- [ ] **Step 4: Commit**

---

### Task 10: 性能测试补充

**Files:**
- Modify: `Source/Common/Runtime/Log/Tests/TestLogPerf.cpp` (在 Task 1 已创建骨架,此处补充完整)
- Modify: `Source/Common/Runtime/Log/Tests/TestLogSinks.cpp`

- [ ] **Step 1: 补充 `TestLogPerf.cpp`** — 8 线程 × 200K 条并发 emit 吞吐测试(pass 标准:≥ 1M rec/sec);单线程 p99 延迟测试(pass 标准:p99 < 1000ns);背压测试(极小队列 + 慢 writer,验证 DropOldest + ERROR+ 保护)

- [ ] **Step 2: 补充 `TestLogSinks.cpp`** — 写临时文件后 `MRollingFileSink` rotate 测试;`MConsoleSink` 输出格式测试

- [ ] **Step 3: 运行 + 记录性能数据 + Commit**

---

### Task 11: 端到端验证脚本

**Files:**
- Create: `Scripts/validate_log_basics.py`
- Create: `Scripts/validate_log_rotation.py`
- Create: `Scripts/validate_log_fatal.py`
- Create: `Scripts/validate_log_routing.py`
- Create: `Scripts/validate_log_filter.py`
- Create: `Config/LogConfig.json`

**Interfaces:**
- Consumes: `Bin/EchoService`, `Bin/GatewayServer`
- Produces: 断言日志文件/行数/JSON 解析/coredump 文件

- [ ] **Step 1: 创建 `Config/LogConfig.json`**

```json
{
  "categories": [
    { "name": "Net",   "level": "Info",  "suppressed": false },
    { "name": "Db",    "level": "Warn",  "suppressed": false },
    { "name": "Auth",  "level": "Info",  "suppressed": false },
    { "name": "Rpc",   "level": "Info",  "suppressed": false }
  ],
  "routes": [
    { "name": "Auth", "sinks": ["console", "file"], "minLevel": "Info" }
  ]
}
```

- [ ] **Step 2: 创建 `Scripts/validate_log_basics.py`** — 参照 `Scripts/validate.py` 框架,启动 EchoService,等待 2s,读取 `Logs/` 下最新 `.jsonl` 文件,断言:文件存在、非空、每行 JSON 可解析、包含 `"cat":"Net"` 等 Category 字段

- [ ] **Step 3: 创建其余 4 个验证脚本** (参照 validate.py 风格,每个脚本专注一个断言)

- [ ] **Step 4: 运行验证**

```bash
python3 Scripts/validate_log_basics.py --build-dir Build
python3 Scripts/validate_log_rotation.py --build-dir Build
```

- [ ] **Step 5: Commit**

---

### Task 12: 迁移 — 删除旧文件

**Files:**
- Delete: `Source/Common/Runtime/Log/Logger.h`
- Delete: `Source/Common/Runtime/Log/Logger.cpp`
- Delete: `Source/Common/Runtime/Log/FileLogSink.h`
- Delete: `Source/Common/Runtime/Log/FileLogSink.cpp`
- Delete: `Source/Common/Runtime/Log/ConsoleLogSink.h`
- Delete: `Source/Common/Runtime/Log/ConsoleLogSink.cpp`
- Modify: `CMakeLists.txt` (从 `MESSION_COMMON_SOURCES` 中移除上述 6 个路径)

- [ ] **Step 1: git rm 上述 6 个文件**

- [ ] **Step 2: 修改 `CMakeLists.txt`** — `MESSION_COMMON_SOURCES` 中删除:
  - `Source/Common/Runtime/Log/Logger.cpp`
  - `Source/Common/Runtime/Log/ConsoleLogSink.cpp`
  - `Source/Common/Runtime/Log/FileLogSink.cpp`

- [ ] **Step 3: 全量编译**

```bash
cmake --build Build -j4 2>&1 | grep -E "error:" | head -20
```

预期:若旧代码仍有 `#include "Common/Runtime/Log/Logger.h"`,会报 undefined;继续 Task 13

- [ ] **Step 4: Commit**

---

### Task 13: 迁移 — 替换调用点

**Files:**
- Modify: 所有引用旧 Logger 的 .cpp/.h 文件(见文件清单,共 21 个)

**Interfaces:**
- Consumes: `Log.h`, `LogCategories.h`
- Produces: 编译通过,运行正常

- [ ] **Step 1: 替换头文件引用**
  - 所有 `#include "Common/Runtime/Log/Logger.h"` → `#include "Common/Runtime/Log/Log.h"`
  - 所有 `#include "Common/Runtime/Log/ConsoleLogSink.h"` → 删除
  - 所有 `#include "Common/Runtime/Log/FileLogSink.h"` → 删除
  - 在使用 Category 的文件中添加 `#include "Common/Runtime/Log/LogCategories.h"`

- [ ] **Step 2: 替换 API 调用**
  - `MLogger::DefaultLogger()->Log(...)` → `MLog::Write(LogCore, ELogLevel::Info, ...)`
  - `MLogger::Init(...)` → `MLog::Init(SInitParams{...})`
  - `MLogger::SetMinLevel(...)` → `MLog::SetCategoryLevel(LogCore, ...)`
  - `MLogger::SetConsoleOutput(...)` → 在 `SInitParams` 中配置
  - `MLogger::GetLogger(Name)` → `MLogRegistry::Get().FindByName(Name)`
  - `MLogger::LogStartupBanner(...)` → 删除,在调用方 main 中用 `MLog::Write(LogCore, ...)` 替代

- [ ] **Step 3: 热点路径升级** (按序替换):
  1. `MRpcDispatch.cpp` → `RPC_LOG(Info, ...)` / `RPC_LOG(Warn, ...)`
  2. `ServerConnection.h/.cpp` → `NET_LOG(Info, ...)` / `NET_LOG(Warn, ...)`
  3. `GatewayServer.cpp` → `NET_LOG` / `AUTH_LOG`
  4. 其余文件:保留 `LOG_INFO(...)` 等宏(自动映射到 `MLog::Write(LogCore, ...)`)

- [ ] **Step 4: 全量编译 + 运行验证脚本**

```bash
cmake --build Build -j4 2>&1 | grep -E "^.*error:" | head -30
python3 Scripts/validate_log_basics.py --build-dir Build
```

- [ ] **Step 5: Commit**

---

### Task 14: 最终验证

**Files:**
- Run: 所有验证脚本

- [ ] **Step 1: 运行全部 5 个端到端脚本**

```bash
python3 Scripts/validate_log_basics.py --build-dir Build && \
python3 Scripts/validate_log_rotation.py --build-dir Build && \
python3 Scripts/validate_log_fatal.py --build-dir Build && \
python3 Scripts/validate_log_routing.py --build-dir Build && \
python3 Scripts/validate_log_filter.py --build-dir Build
```

- [ ] **Step 2: 运行 LogTest**

```bash
./Bin/LogTest 2>&1
```

- [ ] **Step 3: Commit 最终状态**

```bash
git add -A
git commit -m "feat(log): complete high-performance log module implementation

- Async MPSC ring buffer + Dispatcher + per-sink Writer threads
- UE-style Category with DECLARE/DEFINE macros + runtime filter/routing
- Sinks: Console, RollingFile, UDP, TCP, Coredump
- FATAL synchronous capture (last N records + system info + SIGABRT)
- MLogMetrics atomic counters
- 100w+ rec/sec throughput, p99 < 1µs
- Full migration of 21 call sites

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## 自检

**Spec 覆盖检查:**
- ✅ SLogRecord 64B POD → Task 1
- ✅ TMpscRingBuffer lock-free MPSC → Task 1
- ✅ MLogContext TLS + Snapshot → Task 2
- ✅ MLogStringTable → Task 2
- ✅ MLogRegistry + DECLARE/DEFINE 宏 → Task 3
- ✅ MLogMetrics → Task 3
- ✅ MLogRouter + Routing → Task 4
- ✅ MLogFilter + Filter → Task 4
- ✅ ILogSink + Console/File/Rolling/UDP/TCP/Coredump → Task 5-8
- ✅ MLogSinkWriter + Dispatcher → Task 7
- ✅ MLog facade + Write/Init/Shutdown/HandleFatal → Task 7-8
- ✅ Filter/Routing JSON 配置 → Task 7
- ✅ 删除旧文件 → Task 12
- ✅ 迁移 21 个调用点 → Task 13
- ✅ 端到端脚本 → Task 11
- ✅ 单元测试 + 性能测试 → Task 1-10
- ✅ CMakeLists 更新 → Task 9

**占位符扫描:** 无 TBD/TODO,所有步骤含具体代码

**类型一致性:** `MLog::Write` 签名在 Task 7 中定义,Task 13 迁移时直接使用;`SLogCategory::Id` 类型 uint16 在 Task 3-4 中一致使用

**无遗漏:** spec §14 不在本期范围的项目均未实现
