# Mession 日志系统设计规范

> 基于现有 `MLogger` 与业界成熟方案（spdlog、glog、Quill 等）的调研，制定的日志系统设计规范。  
> **约束：零外部依赖**，所有能力在项目内自研实现，不引入 spdlog、fmt、glog 等第三方库。

---

## 一、现有模块调研（Common/Logger.h）

### 1.1 当前能力

| 能力 | 现状 |
|------|------|
| 日志级别 | 5 级：DEBUG(0)、INFO(1)、WARN(2)、ERROR(3)、FATAL(4)，全局 MinLevel |
| 输出目标 | 控制台（stdout）、单文件（追加），二选多可同时 |
| 格式 | 固定：`YYYY-MM-DD HH:MM:SS [LEVEL] message` |
| 线程安全 | 全局 mutex 保护格式化与写入 |
| 配置 | `Init(LogFileName, MinLevel)`、`SetMinLevel`、`SetConsoleOutput` |
| 调用方式 | `LOG_DEBUG/INFO/WARN/ERROR/FATAL(fmt, ...)`，printf 风格 |

### 1.2 当前不足

1. **无模块/上下文**：无法区分日志来源（如 Gateway / World / Net），排查多服时难以过滤。
2. **无源码位置**：无 `file:line` 或 `function`，定位调用点需搜代码。
3. **单文件无轮转**：长时间运行单文件无限增长，无按大小/时间轮转与归档。
4. **同步写**：每次日志都抢锁、格式化、写文件，高并发下易成瓶颈并放大延迟。
5. **无结构化**：仅纯文本，难以被日志采集/检索系统解析（如 JSON、key=value）。
6. **无编译期裁剪**：DEBUG 日志在 Release 仍会做参数求值并进入日志路径，无法零成本关闭。
7. **格式不可配置**：控制台与文件同一格式，无法按目标定制（如控制台简短、文件详细）。
8. **缓冲区与溢出**：固定 4096 字节，过长消息截断无提示；无 flush 策略配置。
9. **无 Fatal 扩展**：FATAL 仅写日志，无 backtrace、abort 或自定义退出逻辑。

---

## 二、业界方案简要对比

| 特性 | 当前 MLogger | spdlog | glog | Quill |
|------|--------------|--------|------|--------|
| 日志级别 | 5 级 | 6 级 + 自定义 | 4 级 + VLOG | 6 级 |
| 多 sink | 否 | 是，多 sink 组合 | 是 | 是 |
| 按大小/时间轮转 | 否 | 是 | 是 | 是 |
| 异步写 | 否 | 是（可选） | 否 | 是（核心） |
| Logger 命名/层级 | 否 | 是 | 否（按模块） | 是 |
| 格式/pattern 可配 | 否 | 是 | 部分 | 是 |
| 源码位置 file:line | 否 | 是（宏） | 是 | 是 |
| 类型安全格式化 | 否（printf） | 是（fmt） | 否（<<） | 是（fmt） |
| 编译期按级别剔除 | 否 | 是（宏） | 是 | 是 |
| 头文件/依赖 | 无 | 头文件+可选 fmt | 需链接 | 需链接 |
| 维护与生态 | 自研 | 活跃 | 基本停滞 | 活跃 |

**结论**：在 **零依赖** 前提下，按本规范在现有 `MLogger` 上自研增强，逐步补齐多 sink、pattern、轮转、编译期裁剪等能力；业界方案仅作能力参考，不引入第三方库。

---

## 三、设计目标与原则

### 3.1 目标

- **可观测**：能按服务/模块/级别过滤，关键路径可追踪（含 file:line 或 logger 名）。
- **可配置**：级别、输出目标、格式、轮转策略可通过配置或 API 变更，无需改代码。
- **低侵入**：现有 `LOG_*` 调用可平滑迁移，宏可保留并映射到新后端。
- **性能可控**：高频路径支持编译期关断低级别日志；生产环境可选用异步 + 轮转，避免阻塞主逻辑。
- **可扩展**：后续可接 syslog、网络 sink、结构化（JSON）等而不推翻整体设计。

### 3.2 非目标（首版可不做）

- 分布式日志采集与检索（由运维/平台侧解决）。
- 日志采样、限流、降级策略（可后续按需加）。
- 与 TraceID 等全链路追踪的深度集成（可预留字段或扩展 pattern）。

---

## 四、功能规范

### 4.1 日志级别

- 级别枚举建议：`Trace` < `Debug` < `Info` < `Warn` < `Error` < `Critical`（与现有 5 级可做一一映射，保留 FATAL 映射到 Critical）。
- 支持全局默认级别 + 按 **logger 名称** 的级别覆盖（如 `Gateway=Debug`、`World.Net=Info`），便于按模块排障。
- 可选：支持按 **sink** 设置最低级别（如控制台只输出 Warn+，文件保留 Info+）。

### 4.2 Logger 命名与获取

- 支持 **命名 logger**（如 `Gateway`、`World`、`World.Net`、`Common.ServerConnection`），便于按名称配置与过滤。
- 提供 **获取默认 logger** 的接口，保证未显式指定时仍有一条明确输出通道。
- 推荐：各服务/模块在初始化时创建或获取同名 logger，日志宏或 API 支持传入 logger 或使用线程局部/全局默认。

### 4.3 输出目标（Sink）

- **控制台**：支持 stdout/stderr 可选，支持按级别着色（可配置关闭）。
- **单文件**：追加写入，路径可配置。
- **轮转文件**：按大小（如 10MB）或按时间（如按天）轮转，保留份数可配置，可选压缩旧文件。
- 单个 logger 可绑定多个 sink；同一进程内多 logger 可共享或独享 sink，由实现决定。

### 4.4 格式（Formatter / Pattern）

- 格式可配置，例如通过 **pattern 字符串** 控制，占位符建议包括：
  - 时间（含毫秒/微秒可选）
  - 级别
  - logger 名称
  - 源码位置（文件:行号 或 文件:行号 函数）
  - 线程 ID（可选）
  - 消息体
- 示例 pattern：`[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [%s:%#] %v`（时间、级别、logger 名、源码、消息）。
- 控制台与文件可使用不同 pattern（如控制台简短、文件带 file:line）。

### 4.5 异步与刷新策略

- **同步**：当前行为，每条日志立即写入，实现简单、顺序严格。
- **异步**（可选）：日志先入队列，后台线程批量写；需保证进程退出或崩溃时尽量不丢关键日志（如 flush 策略、队列大小与阻塞策略）。
- 支持 **按级别或按间隔 flush**，避免长期不刷盘导致宕机丢日志。

### 4.6 编译期裁剪

- 通过宏在编译期剔除低于某级别的日志调用（如 Release 下 strip Debug/Trace），避免参数求值与函数调用，实现零成本关闭。
- 示例：`#if LOG_LEVEL > LOG_LEVEL_DEBUG` 时 `LOG_DEBUG` 展开为空操作。

### 4.7 结构化与扩展

- 首版可以仅支持 **格式化字符串 + 参数**（printf 或类型安全 fmt），不强制要求首版即支持 key=value 或 JSON。
- 设计上预留 **扩展字段**（如 key-value 表）或 **pattern 扩展**，便于后续接结构化采集。

### 4.8 Fatal / Critical 行为

- 在写入日志后，可配置：abort、调用自定义 handler、或仅返回（当前行为）。
- 可选：在 Fatal 时输出简单 backtrace（依赖实现与平台）。

---

## 五、接口与迁移

### 5.1 推荐接口形态

- **宏**：保留 `LOG_DEBUG/INFO/WARN/ERROR/FATAL(fmt, ...)`，内部可映射到默认 logger 或带 `__FILE__`/`__LINE__` 的 API。
- **可选**：支持显式 logger，如 `LOG_LOGGER_INFO(named_logger, fmt, ...)` 或 `named_logger->info(...)`。
- **初始化**：保留或扩展 `Init` 形态，支持从配置文件（路径、级别、sink、pattern）或代码配置。

### 5.2 与现有代码的兼容

- 现有所有 `LOG_*` 调用保持不变（宏名与参数不变），仅替换宏展开实现，指向新后端。
- 若新后端支持「默认 logger 名称」，可在一开始将默认名设为进程名或固定名（如 `Main`），后续再按模块逐步改为命名 logger。

---

## 六、实现路线（零依赖自研）

在 **不引入任何第三方日志库** 的前提下，在现有 `Common/Logger.h` / `Core/NetCore.h` 基础上扩展，按阶段实现下述能力。

### 6.1 阶段一：基础增强（优先）

- **命名 Logger**：支持按名称创建/获取 logger（如 `Gateway`、`World`），默认 logger 保留；现有 `LOG_*` 宏改为使用默认 logger，宏签名不变。
- **源码位置**：宏展开时传入 `__FILE__`、`__LINE__`（可选 `__func__`），在格式中支持占位符输出；不传时输出为空或省略。
- **可配置 pattern**：支持简单 pattern 字符串（如 `%t` 时间、`%l` 级别、`%n` logger 名、`%s` 文件、`%#` 行号、`%v` 消息），仅用 C 标准库与现有 `FString`/`TMap` 实现替换逻辑。
- **多 Sink 抽象**：抽象出 Sink 接口（如 `Write(level, formatted_line)`），内置 ConsoleSink、FileSink；Logger 持有多个 Sink，每条日志向所有 Sink 写入。

### 6.2 阶段二：文件与轮转

- **按大小轮转**：FileSink 支持“单文件最大字节数 + 保留份数”，写满时重命名当前文件（如 `.log.1`）、新建当前 `.log`，循环淘汰最旧。
- **按时间轮转（可选）**：按日或按小时切换文件（如 `mession.20250312.log`），保留天数可配置；可用 `MTime::GetTimeSeconds()` 与本地时间判断。
- **按 Sink 设置级别**：每个 Sink 可设置最低级别，低于该级别的日志不写入该 Sink（如控制台只 Warn+，文件全量）。

### 6.3 阶段三：性能与行为

- **编译期裁剪**：通过 CMake/宏定义 `MESSION_LOG_LEVEL`（如 0=Trace…5=Critical），在头文件中用 `#if MESSION_LOG_LEVEL <= LEVEL_DEBUG` 等将 `LOG_DEBUG` 等展开为空宏（或 `do {} while(0)`），避免参数求值。
- **Flush 策略**：FileSink 支持“每条约 flush / 按间隔 flush / 仅进程退出 flush”等可选策略，减少写放大。
- **Fatal 行为**：FATAL 写完后可配置调用 `std::abort()` 或自定义 handler，不引入 backtrace 库时可仅 abort。

### 6.4 阶段四（可选，后续）

- **异步写**：可选的后台队列 + 工作线程，日志先入队再由线程批量写 Sink；队列有界、满时策略可为阻塞或丢弃。
- **结构化**：预留“附加 key=value”或单行 JSON 的扩展点，首版可不实现。

实现时仅使用：C++20 标准库、本项目已有类型（`FString`、`TMap`、`TVector`、`MTime` 等）、以及平台 API（如 `fopen`/`fwrite`/`flock` 或 C++ `std::ofstream`），不依赖 spdlog、fmt、glog 等。

---

## 七、配置示例（目标形态）

```ini
# 示例：日志配置（具体键名与实现绑定）
[log]
level = info
default = Gateway,World,Login,Scene,Router

[log.console]
enabled = true
level = info
pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] %v"

[log.file]
enabled = true
path = logs/mession.log
level = debug
pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [%s:%#] %v"
rotation_size_mb = 10
rotation_count = 5
```

---

## 八、文档与后续

- 本规范作为 **设计约束**：实现需满足第四节功能规范、第五节接口与迁移，且 **零外部依赖**，按第六节阶段推进。
- 实现落地后，在 `README` 或 `docs/` 中补充：如何配置级别、路径、轮转、pattern，以及如何按 logger 名过滤。

---

*文档版本：1.1；约束为零依赖，实现路线为自研增强、分阶段落地。*
