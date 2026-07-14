# C++ 代码风格规范 (Coding Style)

> 适用范围:`Source/`、`Protocol/`、`tests/` 下所有 C++ 源码。
> 强制性:本规范由 `.clang-format` + pre-commit + `scripts/check-style.sh` 机器强制;任何与之冲突的个人习惯以本文档为准。
> 配套:本规范的**设计依据与背景**见 `Docs/superpowers/specs/2026-07-14-coding-style/design.md`。

---

## 0. 快查表

| 类别 | 规则 |
|------|------|
| 类 | `M*` |
| 结构 | `S*` |
| 枚举 | `E*` |
| 纯虚接口 | `I*` |
| bool 字段/变量/参数 | `bXxx` |
| 静态成员 | 与普通成员同命名,无前缀 |
| 函数/方法/局部变量/参数 | PascalCase |
| 模板参数 | `T`/`TValue`/`TKey`/`TIterator` |
| 入参/出参 | `InXxx`/`OutXxx` |
| 全局指针/变量 | `G` 前缀 + PascalCase |
| 常量 | 大写蛇形,允许下划线(豁免) |
| 其他标识符 | **禁止下划线** |
| 命名空间 | 小写连写 |
| 缩进 | 4 空格 |
| 列宽 | 240 |
| 花括号 | Allman 全开,禁止 `if (x) foo;` |
| 指针星号 | 左对齐 `T* Ptr` |

---

## 1. 命名规约

### 1.1 类型前缀

| 类别 | 前缀 | 示例 |
|------|------|------|
| 类 | `M*` | `MPlayerService`、`MGatewayServer` |
| 结构(纯数据/POD) | `S*` | `SPlayerConfig`、`SEchoServiceConfig` |
| 枚举 | `E*`(包含 `enum class` 与 plain `enum`) | `EServerType`、`EMessageType` |
| 纯虚接口 | `I*` | `INetConnection`、`IRpcChannel` |

> **反例**:`class Gateway` ❌ → `class MGateway` ✓

### 1.2 bool 字段、局部变量、参数

统一 `bXxx`,**必须以名词结尾**,不允许 `bIsXxx`/`bHasXxx`/`bShouldXxx`(`is/has/should` 这些语义已隐含在 `b` 前缀里,重复就是冗余)。

```cpp
// ✓ 正确
bool bRunning = false;
bool bHealthy = true;
bool bConnected = true;
bool bRegistered = false;

// ✗ 错误
bool Running = false;        // 缺 b 前缀
bool isConnected = true;     // 缺 b 前缀,且用 snake
bool bIsConnected = true;    // 重复 is-
bool bHasError = false;      // 重复 has-
```

### 1.3 静态成员

**不加任何前缀**,与普通成员同命名,PascalCase。

```cpp
class MEndpointCache
{
public:
    static MEndpointCache& Get();   // ✓
    static constexpr int32 MaxRetries = 3;  // ✓
};
```

### 1.4 函数、方法、局部变量、参数

PascalCase,动词或动词短语开头:

```cpp
void Tick();
void HandleClientPacket(uint64 ConnectionId, const TByteArray& Data);
bool EncodeEndpoint(const FServiceEndpoint& Ep, TByteArray& Out);
```

### 1.5 模板参数

短类型用 `T`,语义化用 `TValue`/`TKey`/`TIterator`/`TPredicate`:

```cpp
template<typename T>
TVector<T> MapValues(const TMap<T, int>& In);

template<typename TValue, typename TPredicate>
TVector<TValue> Filter(TVector<TValue> In, TPredicate Pred);
```

### 1.6 入参/出参

- 输入参数:`InXxx`
- 输出参数:`OutXxx`
- 输入输出参数:`InOutXxx`(少见,优先用返回值)

```cpp
bool EncodeEndpoint(const FServiceEndpoint& InEp, TByteArray& OutBuffer);
void ParseAddrPort(const MString& In, MString& OutAddr, uint16& OutPort);
```

### 1.7 全局指针/变量

`G` 前缀 + PascalCase:

```cpp
MGatewayServer* GGlobalGateway = nullptr;
MEchoService*   GGlobalEchoService = nullptr;
```

### 1.8 常量(豁免)

`constexpr` / `const` 全局常量用大写蛇形,**允许下划线**(全规约唯一例外):

```cpp
constexpr uint32 MAX_PACKET_SIZE = 65535;
constexpr uint32 MAX_PLAYER_COUNT = 10000;
constexpr float  DEFAULT_TICK_RATE = 1.0f / 60.0f;
```

### 1.9 其他标识符(变量、字段、函数、类型)

**禁止下划线**(常量除外):

```cpp
// ✗ 错误
int player_count = 0;
int32 m_nHealth = 100;
void handle_client_packet();

// ✓ 正确
int PlayerCount = 0;
int32 Health = 100;
void HandleClientPacket();
```

### 1.10 命名空间

小写 + 单词连写,无下划线,无 PascalCase:

```cpp
namespace mession::net { ... }       // ✓
namespace Mession::Net { ... }       // ✗ 错(PascalCase)
namespace mession_net { ... }        // ✗ 错(下划线)
```

---

## 2. 头文件与 include

### 2.1 头文件保护

统一 `#pragma once`,不使用传统的 `#ifndef` 守卫(本项目已统一,本规范只是明确)。

### 2.2 include 顺序

按 clang-format `IncludeCategories` 分四组,**组间空一行**,同组字典序:

1. **当前文件对应头**:`Foo.cpp` 第一行是 `#include "Foo.h"`
2. **本项目头**:`"Common/..."`、`"Servers/..."`、`"Protocol/..."`(相对 `Source/`)
3. **第三方库**:`<boost/...>`、`<gtest/...>`、`<fmt/...>`
4. **C++ 标准库**:`<vector>`、`<memory>`、`<string>`

```cpp
// ✓ 正确
#include "Common/Net/ServiceDiscovery/Endpoint.h"

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/Logger.h"

#include <map>
#include <vector>
```

不要混用引号和尖括号表达本项目/标准库的区别——引号是本项目,尖括号是第三方或标准库。

### 2.3 头文件最小化

- 头文件中只放类型声明、模板、inline 函数。
- 实现细节放 `.cpp`。
- 不要 `#include` 不必要的头(用前向声明替代)。

---

## 3. 格式化

### 3.1 缩进

- 4 空格,**禁用 Tab**。
- 命名空间缩进:`namespace { ... }` 内部内容统一缩进(`NamespaceIndentation: All`)。

### 3.2 列宽

- 单行最大 **240 列**。超过则换行。

### 3.3 花括号 (Allman 风格)

**所有**花括号都换行(类、结构、枚举、函数、`if`/`for`/`while`/`do`/`switch` case、`try`/`catch`、`namespace`):

```cpp
// ✓ 正确(Allman)
class MPlayerService
{
public:
    void Tick()
    {
        if (bRunning)
        {
            DoWork();
        }
        else
        {
            Stop();
        }
    }
};

// ✗ 错误(K&R / 单行)
class MPlayerService {
public:
    void Tick() {
        if (bRunning) { DoWork(); }
    }
};
```

**禁止**单行 `if/for/while`,即使语句只有一条(与 `CLAUDE.md` 一致):

```cpp
// ✗ 错误
if (bRunning) DoWork();
for (auto& X : List) Process(X);

// ✓ 正确
if (bRunning)
{
    DoWork();
}
for (auto& X : List)
{
    Process(X);
}
```

### 3.4 指针与引用

**星号靠左,与类型贴合**(`PointerAlignment: Left`):

```cpp
int* Ptr = nullptr;          // ✓
const MString& Name = "...";  // ✓
TSharedPtr<IFoo> Foo;        // ✓

int *Ptr = nullptr;           // ✗
const MString &Name = "...";  // ✗
```

### 3.5 连续赋值与声明对齐

同区域内连续赋值、声明、宏调用开启对齐(`AlignConsecutiveAssignments: true` 等):

```cpp
int32 LocalServerId   = 0;
int32 LocalInstId     = 0;
MString LocalAddress  = "0.0.0.0";
uint16 LocalPort      = 0;
```

> 注:clang-format 会按"块"判定(被空行或非赋值语句分隔),不必手动对齐所有变量。

### 3.6 include 排序

clang-format `SortIncludes: true` + `IncludeBlocks: Preserve`,同组字典序,组间空行(见 § 2.2)。

### 3.7 类访问修饰符

`public:`/`private:`/`protected:` **顶格**(无缩进,`AccessModifierOffset: 0`)。

### 3.8 空行

- 函数体内最多保留 2 个连续空行(`MaxEmptyLinesToKeep: 2`)。
- 代码块开头不留空行(`KeepEmptyLinesAtTheStartOfBlocks: false`)。

---

## 4. 控制流

### 4.1 强制花括号

与 `CLAUDE.md` 一致:**所有 `if`/`for`/`while` 必须使用花括号**,即使单语句。

### 4.2 早返回

鼓励"早返回"减少嵌套:

```cpp
// ✓ 推荐
bool TryConnect(const MString& Addr, uint16 Port)
{
    if (Addr.empty())
    {
        return false;
    }
    if (Port == 0)
    {
        return false;
    }
    // ... do work
    return true;
}

// ✗ 不推荐(嵌套过深)
bool TryConnect(const MString& Addr, uint16 Port)
{
    if (!Addr.empty())
    {
        if (Port != 0)
        {
            // ... do work
            return true;
        }
    }
    return false;
}
```

### 4.3 switch / case

`case` 块必须用花括号包住(避免变量作用域泄露);`default` 必须存在(即使是空 `break;`)。

```cpp
switch (PacketType)
{
    case MT_RPC:
    {
        HandleRpc(Payload);
        break;
    }
    case MT_FunctionCall:
    {
        HandleFunctionCall(Payload);
        break;
    }
    default:
    {
        LOG_WARN("unknown packet type: %u", static_cast<unsigned>(PacketType));
        break;
    }
}
```

---

## 5. 错误处理

### 5.1 三种方式

| 场景 | 推荐方式 |
|------|----------|
| 业务层、RPC 入口 | `MResult` / `TResult<T, E>`(定义见 `Common/Runtime/Object/Result.h`) |
| 协议层底层、解析/序列化 | 异常 + `MFuture` / `FAppError` |
| 内部 bool 检查、守卫 | `bool` 返回值 + 早返回(只用于不影响主流程的探测) |

**禁止**:
- 返回 `int -1`/`int 0` + 隐式 out-param 模拟错误码
- 抛异常吞掉(`catch (...) {}` 不打日志)
- `assert` 替代业务错误检查(`assert` 在 `Release` 被编译掉,业务错误必须显式处理)

### 5.2 `MResult` 用法

```cpp
MResult<int32> ParsePort(const MString& In)
{
    if (In.empty())
    {
        return MResult<int32>::Fail(FAppError::Make(EAppErrorCode::InvalidArg, "empty input"));
    }
    // ...
    return MResult<int32>::Ok(8080);
}

// 调用方
auto Result = ParsePort(Config.ListenAddr);
if (!Result.IsOk())
{
    LOG_ERROR("ParsePort failed: %s", Result.GetError().What());
    return false;
}
const int32 Port = Result.GetValue();
```

### 5.3 `LOG_FATAL` 后必须退出

```cpp
// ✓ 正确
LOG_FATAL("registry unreachable");
std::abort();  // 或 return EXIT_FAILURE; / throw

// ✗ 错误
LOG_FATAL("registry unreachable");
// 继续执行 ❌
```

---

## 6. 日志

### 6.1 五档固定

| 级别 | 用途 |
|------|------|
| `LOG_DEBUG` | 调试细节,高频路径(`Tick` 内) |
| `LOG_INFO` | 关键流程节点:启动、停止、连接成功、注册成功 |
| `LOG_WARN` | 可恢复的异常:重连、重试、降级 |
| `LOG_ERROR` | 不可恢复但程序可继续:解析失败、RPC 调用失败 |
| `LOG_FATAL` | 必须退出的致命错误:配置错误、关键依赖不可用 |

### 6.2 printf 风格,禁用流式

```cpp
// ✓ 推荐
LOG_INFO("Server started on port %u, inst=%u", static_cast<unsigned>(Port), InstId);
LOG_DEBUG("Tick delta=%.3fms, connCount=%zu", DeltaMs, Connections.size());

// ✗ 避免(临时字符串 + operator<< 链)
LOG_INFO("Server started on port " << Port << ", inst=" << InstId);
```

### 6.3 必须打日志的位置

- 服务入口:`Init()`、`OnRunStarted()`
- 退出路径:`Shutdown()`、`OnRunStopped()`
- 错误路径:返回 `false` 前、`MResult::Fail` 前
- 网络事件:连接建立/断开、握手成功/失败
- 关键业务事件:注册、心跳、配置变更

### 6.4 高频路径必须用 `DEBUG`

`Tick()`、`OnTick()`、`Update()` 等每帧调用的方法,日志必须是 `LOG_DEBUG`(`LOG_INFO` 会刷爆日志)。

---

## 7. 注释与文件头

### 7.1 文件头 Doxygen 块

每个 `.h`/`.cpp` 顶部必须有:

```cpp
/**
 * @file Endpoint.h
 * @brief FServiceEndpoint wire types and Registry encode/decode helpers.
 */
```

`.cpp` 的 `@brief` 可以与 `.h` 略不同(`.h` 描述"是什么",`.cpp` 描述"做什么")。

### 7.2 类/结构注释

```cpp
/**
 * @brief MEndpointCache - Service-side service-discovery cache.
 *
 * Holds Registry TCP client + per-ServerType endpoint snapshots + connection pool.
 *
 * @detail 生命周期:
 *   1. BindRegistry(Addr, Port)
 *   2. RegisterLocal(SelfEndpoint)
 *   3. Tick(DeltaTime) — 每帧
 *   4. DeregisterAndShutdown()
 */
class MEndpointCache
{
    // ...
};
```

### 7.3 方法注释

有 `@param`/`@return` 的公共方法:

```cpp
/**
 * @brief ParseAddrPort - 把 "host:port" 拆成 host + port.
 * @param In   "host:port" 字符串
 * @param OutAddr  拆出的 host
 * @param OutPort  拆出的 port
 * @return true 解析成功,false 格式错误
 */
bool ParseAddrPort(const MString& In, MString& OutAddr, uint16& OutPort);
```

### 7.4 行内注释

- 解释**为什么**(why)而不是**做什么**(what)。
- 复杂逻辑的"陷阱"必须注释(例如 `if (Port == 0) /* 允许 0 = 任意端口 */`)。

---

## 8. 容器与智能指针

### 8.1 STL 别名

**永远用项目别名**,不要直接用 `std::vector` 等(在 `Source/Common/Runtime/MLib.h` 已定义):

```cpp
TVector<int>        // 而不是 std::vector<int>
TMap<K, V>          // 而不是 std::map<K, V>
TUnorderedMap<K, V> // 而不是 std::unordered_map<K, V>
TSharedPtr<T>       // 而不是 std::shared_ptr<T>
TWeakPtr<T>         // 而不是 std::weak_ptr<T>
TUniquePtr<T>       // 而不是 std::unique_ptr<T>
```

新增别名 → 先加到 `MLib.h`。

### 8.2 智能指针构造

统一用 `MakeShared<T>(...)`,**不要**用 `TSharedPtr<T>(new T(...))`:

```cpp
// ✓ 正确
auto Cache = MakeShared<MEndpointCache>();

// ✗ 错误
TSharedPtr<MEndpointCache> Cache(new MEndpointCache());
```

### 8.3 接口赋值

```cpp
TSharedPtr<IFoo> Ptr = MakeShared<MImpl>(...);  // ✓ MakeShared 返回的 TSharedPtr<MImpl> 隐式转 TSharedPtr<IFoo>
```

### 8.4 容器预分配

已知大小时调用 `reserve()`,减少 realloc:

```cpp
TVector<FServiceEndpoint> Result;
Result.Reserve(Endpoints.size());
for (const FServiceEndpoint& Ep : Endpoints)
{
    Result.PushBack(Ep);
}
```

---

## 9. 反射与序列化(ABI 保护)

### 9.1 反射字段名是公开 ABI

`MPROPERTY()` 标注的字段名是 RPC 协议和存档格式的一部分。**禁止重构中改名/重排/删除**。

```cpp
// ✓ 允许(只改格式)
MSTRUCT()
struct FServiceEndpoint
{
    MPROPERTY() EServerType ServerType = EServerType::Unknown;
    MPROPERTY() uint32 ServerId = 0;
    MPROPERTY() MString Address = "0.0.0.0";
    MPROPERTY() uint16 Port = 0;
};

// ✗ 禁止(改名会破坏 ABI)
// MPROPERTY() MString Addr = "0.0.0.0";
```

### 9.2 反射字段顺序

二进制序列化(参考 `Endpoint.cpp` `EncodeEndpoint`)依赖字段顺序。新增字段必须**追加**到末尾,**不能**插队。

### 9.3 `MSTRUCT + MPROPERTY` 协议结构

**优先**用 `MSTRUCT + MPROPERTY` 替代手动序列化(参见 `CLAUDE.md` "Protocol Structures" 一节)。新协议禁止手写 `Encode*/Decode*` 函数,除非 `MHeaderTool` 不支持的格式。

---

## 10. 工具链

### 10.1 clang-format

根目录 `.clang-format` 自动格式化所有 `.h`/`.cpp`/`Tests/` 下的 C++ 文件。

```bash
# 格式化单个文件
clang-format -i Source/Common/Net/ServiceDiscovery/Endpoint.cpp

# 检查但不动(给 CI 用)
clang-format --dry-run --Werror Source/Common/Net/ServiceDiscovery/Endpoint.cpp
```

### 10.2 pre-commit

`.pre-commit-config.yaml` 注册 `clang-format` hook,`git commit` 时自动检查暂存区:

```bash
# 一次性安装
bash scripts/install-hooks.sh

# 之后每次 commit,pre-commit 会跑 clang-format
# 失败:git commit 被拒,运行 clang-format -i <file> 修复后重试
```

### 10.3 CI 检查

`scripts/check-style.sh` 对 `Source/` 全部 `.h`/`.cpp` 跑 `clang-format --dry-run --Werror`,失败非零退出。CI 必跑这一步。

### 10.4 Build/Generated/ 排除

MHeaderTool 生成的代码在 `Build/Generated/`,**不参与**风格检查。

---

## 11. 例外与豁免

| 类别 | 豁免规则 |
|------|----------|
| 常量 | 大写蛇形,允许下划线 |
| `Build/Generated/` | 自动生成,跳过检查 |
| 第三方库头 | 不修改上游代码 |
| 已有反射字段 | 禁止改名/重排,只允许格式化 |

---

## 12. 相关文档

- `CLAUDE.md`:项目总纲,本规范的快查表 + 跳转。
- `Docs/Architecture.md`:系统架构,Server 拓扑、对象模型。
- `Docs/RuntimeAndRpc.md`:反射系统与 RPC 派发。
- `Docs/superpowers/specs/2026-07-14-coding-style/design.md`:本规范的设计依据。
