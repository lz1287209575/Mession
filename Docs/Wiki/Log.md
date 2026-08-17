# MLog 日志模块

MLog 是 Mession 的日志子系统,基于无锁 ring buffer 的异步管道,支持 Category 路由、文件轮转、远程收集与 FATAL coredump。调用点不做任何磁盘/网络 I/O,吞吐 ≥100 万条/秒(8 线程)、enqueue p99 < 1µs(有性能测试验证)。

## 架构总览

```
[业务线程] ──► MLog::Write ──► Filter(级别+静默) ──► Router(SinkMask) ──► TMpscRingBuffer<SLogRecord>
                                                                                │
                                                                                ▼
                                                          MLogDispatcher(mlog-dispatch)
                                                                                │ 按 SinkMask fan-out
                                       ┌────────────┬─────────────┬─────────────┴────────────┐
                                       ▼            ▼             ▼                          ▼
                              MConsoleSink    MRollingFileSink   MUdpSink/MTcpSink     (FATAL 不走队列)
                              mlog-console    mlog-file-writer   mlog-udp/tcp-writer   MCoredumpSink(同步)

[业务线程 FATAL] ──► MLog::HandleFatal ──► 同步采集最近 N 条 + 系统信息 + backtrace ──► raise(SIGABRT)
```

核心数据流:

- **`MLog::Write`(热路径)**:调用点无锁——先按 Category 过滤(级别 + `bSuppressed`),再查路由表得 `SinkMask`,然后 `SLogRecord`(固定 64B POD,含 SSO ≤16B 内联消息或外溢标记)enqueue 进 `TMpscRingBuffer`。
- **`TMpscRingBuffer<SLogRecord>`**:无锁多生产者单消费者环形队列,默认容量 `1<<16` 条(约 4MB),满时按 `EEvictionPolicy` 处理(默认 `DropOldest`,ERROR+ 受保护不被覆盖)。
- **`MLogDispatcher`**:单消费者线程,批量 pop(默认 256 条)后按 `SinkMask` 分桶,整批投递到各 sink 的独立 inbox。
- **`MLogSinkWriter`(每 sink 一个线程)**:从 inbox 取批 → 序列化(console 文本 / 文件与远端 JSON Lines)→ 写目标 → 按 `EFlushPolicy`(默认 `IntervalOrSize`:10ms 定时或 64KB 阈值)触发 `Flush`。
- **`MLogContext`(TLS,`MLogContextScope` RAII)**:每线程附带键值上下文(如 `actor_id`、`req_id`),enqueue 时一次性快照;用法:

```cpp
{
    MLogContextScope ActorScope("actor_id", "1001");
    NET_LOG(Info, "user login");
}
```

## 日志级别与宏用法

级别枚举 `ELogLevel`:`Trace(0)` / `Debug(1)` / `Info(2)` / `Warn(3)` / `Error(4)` / `Critical(5)`(Critical 即 FATAL)。

### 业务 Category 宏

```cpp
NET_LOG(Info, "connected %s", ip);     // LogNet
DB_LOG(Warn, "slow query %lld ms", ms); // LogDb
RPC_LOG(Error, ...);                    // LogRpc
AUTH_LOG(Info, ...);                    // LogAuth
SCENE_LOG(Info, ...);                   // LogScene
CORE_LOG(Info, ...);                    // LogCore
```

### 过渡宏(映射到 LogCore,printf 风格,自动带 `__FILE__/__LINE__/__func__`)

```cpp
LOG_DEBUG(...);  LOG_INFO(...);  LOG_WARN(...);  LOG_ERROR(...);
LOG_FATAL(...);  // → LogCore @ Critical,仍走异步队列
```

### FATAL 同步宏(走 coredump 路径,不用异步队列)

```cpp
LOG_FATAL_EX(LogNet, "fatal state: %d", code);
// → MLog::HandleFatal:同步采集最近 N 条 + 系统信息 + backtrace,
//   写 Logs/coredump/<service>-<ts>.jsonl,flush 后 raise(SIGABRT)
```

### 初始化 / 生命周期

```cpp
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Log/LogCategories.h"

MLog::Init(SLogInitParams{...});   // 启动期一次;重复调用 no-op
// ... 业务运行 ...
MLog::Flush();                     // 可选,退出前排空(默认超时 1s)
MLog::Shutdown();                  // drain(5s 超时)+ join 所有线程;可多次调用
```

## 配置

### 代码配置(`SLogInitParams`)

| 字段 | 默认 | 说明 |
|---|---|---|
| `ConfigPath` | 空 | JSON 配置文件路径,存在则解析 |
| `GlobalDefaultLevel` | `Info` | 全局默认级别 |
| `bUseColor` | `false` | console 是否 ANSI 着色 |
| `LogDir` | `"Logs"` | 文件 sink 目录 |
| `FilePath` | 空 | 实时日志文件路径(如 `"Logs/echo.jsonl"`);为空则不启用文件 sink |
| `RotatedFileBytes` | `100MB` | 滚动阈值 |
| `NumArchives` | `5` | 保留归档数 |
| `bEnableUdp` / `UdpTarget` | `false` / `"ip:port"` | UDP 远程收集 |
| `bEnableTcp` / `TcpTarget` | `false` / `"ip:port"` | TCP 远程收集 |
| `bEnableConsole` | `true` | 控制台 sink 开关 |
| `RingCapacity` | `1<<16` | ring buffer 容量(条数) |

### JSON 配置(启动期加载)

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

- `categories`:按名设置 Category 运行时级别 / 静默(静默时该 Category 日志完全不入队,并计入 `SuppressedByCategory` 计数)。
- `routes`:按名设置 Category 的 sink 集合(合法名:`console` / `file` / `udp` / `tcp` / `coredump`)与最低级别。
- 文件缺失或解析失败:`MLogApplyConfigFile` 返回 `false` 且**不改变任何输入**,回退默认级别,启动不阻塞。
- 运行时也可直接调用:`MLog::SetCategoryLevel` / `MLog::SetCategorySuppressed` / `MLog::ApplyCategoryConfig` / `MLog::ApplyRouteConfig`(已存在规则除非被同名覆盖否则保留)。

### 文件 sink 行为

- `MRollingFileSink` 以 JSON Lines 写日志(每条一个 JSON 对象),整批一次 `fwrite`(Unix 下单次 syscall)。
- 按大小滚动:当前文件字节数 + 下一条将超阈值即关闭并改名为 `<path>.<N>`(N 递增),删除超出 `NumArchives` 的最旧归档后开新文件;跨进程/跨运行以追加方式续写。
- `Flush()` = `fflush` + 每 `FlushesPerFsync`(默认 10)次做一次 `fdatasync`。
- sink `Open()` 失败不致命:记 stderr 并跳过该 sink,后续 `WriteBatch` no-op;单 sink 故障不影响其它 sink 与业务线程。

## 验证方式

- 测试可执行文件:**`Bin/LogTest`**(构建产物),`main()` 顺序跑全部测试并汇总;测试 TU 位于 `Source/Common/Runtime/Log/Tests/`,由 `main.cpp` 直接 include。
- 覆盖项(`TestMpscRingBuffer` / `TestLogContext` / `TestLogRegistry` / `TestLogMetrics` / `TestLogRouter` / `TestLogSinks` / `TestLogPerf`):
  - ring buffer 基本读写、多生产者并发、满时返回 false;
  - `MLogContext` 设置/取消、嵌套 scope;
  - `MLogRegistry` 注册/按 Id 查找/按名查找;
  - `MLogMetrics` 计数累加与 snapshot 一致性;
  - `MLogRouter` 默认全 sink、规则改掩码、MinLevel 过滤;
  - console sink 文本批输出、rolling file 写 JSON Lines;
  - 性能断言:enqueue p99 < 1µs、8 线程并发吞吐 ≥ 100 万条/秒、背压下 `DropOldest` 保护 ERROR+。
- 端到端脚本(`Scripts/validate_log_*.py`):日志基础(文件存在/行数/Category 标签/JSON 合法)、轮转(写满后归档数 ≤ `NumArchives`)、FATAL(coredump 文件存在且含触发记录)、路由(Auth 只走 file 时 console 无 Auth 行)、过滤(静默 Category 无输出且计数增加)。

## 相关实现

- 核心头文件(公共入口):
  - `Source/Common/Runtime/Log/Log.h` — facade 与全部 `LOG_*` / `*_LOG` 宏
  - `Source/Common/Runtime/Log/LogLevel.h` — `ELogLevel`、`EEvictionPolicy`、`EFlushPolicy`
  - `Source/Common/Runtime/Log/LogRecord.h` — `SLogRecord`(64B POD)
  - `Source/Common/Runtime/Log/MpscRingBuffer.h` — `TMpscRingBuffer`(header-only)
  - `Source/Common/Runtime/Log/LogCategory.h` / `LogCategories.h` / `LogCategories.cpp` — Category 声明/定义/项目级分类
  - `Source/Common/Runtime/Log/LogConfig.h` / `LogConfig.cpp` — `SLogInitParams` + JSON 解析
- 机制实现:
  - `Source/Common/Runtime/Log/LogContext.h` / `.cpp` — `MLogContext` TLS + `MLogContextScope`
  - `Source/Common/Runtime/Log/LogRegistry.h` / `.cpp` — `MLogRegistry`
  - `Source/Common/Runtime/Log/LogRouter.h` / `.cpp` — `MLogRouter`
  - `Source/Common/Runtime/Log/LogFilter.h` — `MLogFilter`
  - `Source/Common/Runtime/Log/LogMetrics.h` / `.cpp` — `MLogMetrics`
  - `Source/Common/Runtime/Log/LogStringTable.h` / `.cpp` — 源文件/函数字符串 intern
  - `Source/Common/Runtime/Log/Dispatcher.h` / `.cpp` — `MLogDispatcher` 线程
  - `Source/Common/Runtime/Log/SinkWriter.h` / `.cpp` — `MLogSinkWriter` 线程
- Sink 实现:
  - `Source/Common/Runtime/Log/LogSinks.h` — `ILogSink` 接口
  - `Source/Common/Runtime/Log/ConsoleSink.h` / `.cpp` — `MConsoleSink`
  - `Source/Common/Runtime/Log/RollingFileSink.h` / `.cpp` — `MRollingFileSink`
  - `Source/Common/Runtime/Log/UdpSink.h` / `.cpp`、`TcpSink.h` / `.cpp` — 远程 JSON Lines
  - `Source/Common/Runtime/Log/CoredumpSink.h` / `.cpp` — `MCoredumpSink`(FATAL 同步路径)
- 测试:
  - `Source/Common/Runtime/Log/Tests/`(`main.cpp` + `TestHarness.h` + 7 个测试 TU)
  - 产物 `Bin/LogTest`;端到端脚本 `Scripts/validate_log_basics.py`、`validate_log_rotation.py`、`validate_log_fatal.py`、`validate_log_routing.py`、`validate_log_filter.py`
- 设计文档:`Docs/superpowers/specs/2026-07-14-log-module-design.md`
