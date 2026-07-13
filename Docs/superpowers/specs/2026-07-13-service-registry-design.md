# MServiceRegistry + MEndpointCache — 设计 Proposal

> 起草：2026-07-13
> 状态：v1
> 关联：`/root/Mession/Docs/superpowers/specs/2026-07-07-actor-rpc-refactor.md`（同质多进程 + Actor-based RPC 重构）

---

## 0. 一句话

去掉 `SEchoServiceConfig::Peers` 这种**静态写死**的 peer 列表，新增独立 `MServiceRegistry` 进程做集中服务发现；Service 侧通过 `MEndpointCache`（lazy connect + 缓存）按 `EServerType` 拿 endpoint，按 `DEBUG` / `RELEASE` 区分失败行为。

## 1. 目标

1. **集中服务发现**：3 个 binary——`Gateway` / `EchoService` / `MServiceRegistry`。前两者通过 `--registry=addr:port` 注册到 Registry；后者是独立监听进程，纯内存（PoC 不持久化）。
2. **心跳 + 推送**：Service 每 5s 发心跳；Registry 在 15s / 30s 阈值推 `EndpointChange` 给关注同 ServerType 的 Service。
3. **懒连接**：Service 第一次通过 `GetOrConnect` 触发连接；后续复用 connection pool。Endpoint 变更走增量更新。
4. **DEBUG vs RELEASE 失败分级**：Registry 不可达时，DEBUG 直接 `LOG_FATAL` 退出（迭代快），RELEASE 降级启动——缓存上次快照 + 后台重连，业务侧 `GetOrConnect` 返 `nullptr`，调用方走 RPC 失败重试。
5. **删除 `SEchoServiceConfig::Peers` 静态字段**：peer 地址硬编码在 CLI 是反模式。

## 2. 非目标

- Registry 持久化（PoC 纯内存，进程退出 = 数据全丢）。
- 跨主机的 Service 自动发现（需要 K8s service / DNS / etcd，单独 spec）。
- Registry 副本 / 共识 / HA（单点足够 PoC）。
- Registry 鉴权 / mTLS（trusted LAN 内部组件）。
- 业务层 RPC 重试策略（沿用 `MServerConnection` 现有重连 + `MRpcChannel` 现有错误传播）。
- Registry 客户端断线降级时 RPC 缓存（PoC 失败 → 调用方负责 retry）。

## 3. 现状基线

| 类别 | 状态 |
|------|------|
| **底层** | `MServerConnection`（TCP 连接 + 重连 tick）/`MTcpMessageChannel`/`MEndpointCache` 尚未存在 |
| **路由** | `MServerRuntimeContext::ResolveServerTransport` 查进程内 `TMap<EServerType, TSharedPtr<MServerConnection>>`；`MActorRouter` 查进程内 `TMap<ActorId, SActorRoute>` |
| **Service 配置** | `SEchoServiceConfig::Peers: TVector<SServicePeerConfig>`（CLI 写死，标 `MSTRUCT`，reflection 解析）；`EchoService::ConnectAllPeers()` 启动时遍历建连 |
| **包格式** | `EServerMessageType` enum（MT_RPC / MT_FunctionCall / MT_FunctionResponse）；TCP frame = `1 byte type + 1 byte reserved + length-prefixed payload` |
| **Logger** | `LOG_DEBUG/INFO/WARN/ERROR/FATAL` 已存在（`Common/Runtime/Log/Logger.h`） |

## 4. 目标架构

```
+---------------------+   +------------------------+   +----------------------+
|  Gateway            |   |  EchoService A         |   |  EchoService B        |
|  --registry=...:    |   |  --registry=...:       |   |  --registry=...:      |
+---------------------+   +------------------------+   +----------------------+
            \                       |   |                          /
             \                      |   |                         /
              \_____________________|   |________________________/
                                       |
                              +-----------------------+
                              |   MServiceRegistry    |
                              |  --listen=18000      |
                              |  (pure in-memory)    |
                              +-----------------------+
```

`MEndpointCache` 是 Service 进程内的单例——缓存 Registry 推来的 endpoint 列表 + 维护 connection pool + 懒连接。

## 5. Registry 进程接口

### 5.1 数据结构

```cpp
// Common/Net/ServiceDiscovery/Endpoint.h
struct FServiceEndpoint {
    EServerType ServerType;     // Gateway / Echo
    uint32 ServerId;             // 唯一 ID（Service 启动时 MUniqueIdGenerator::Generate）
    MString Address;             // PoC: "0.0.0.0"
    uint16 Port;
    uint64 LastHeartbeatMs;
    bool bHealthy;
    TVector<uint64> ActorIds;    // 该实例持有的 ActorId（走 MServiceId::Make 转）
};
```

### 5.2 Registry 进程入口

```
$ ./Bin/MServiceRegistry --listen=18000
```

**核心数据结构**（Registry 内部）：

```cpp
class MServiceRegistry {
public:
    static MServiceRegistry& Get();

    // Service 侧 RPC（双向，Registry 是 server）
    void Register(const FServiceEndpoint& Endpoint);
    void Deregister(uint32 ServerId);
    void Heartbeat(uint32 ServerId);
    void UpdateActors(uint32 ServerId, const TVector<uint64>& ActorIds);

    // Registry 主动 push
    void BroadcastEndpointChange(EServerType ServerType);

    // 进程内查询
    TVector<FServiceEndpoint> GetHealthyEndpoints(EServerType ServerType) const;

    // 主循环 tick（心跳超时检测）
    void Tick(float DeltaTime);

private:
    TMap<uint32, FServiceEndpoint> Endpoints_;  // by ServerId
    TMap<EServerType, uint64> MonotonicSeq_;   // push 序号（乱序检测）
    TSet<TSharedPtr<MTcpConnection>> Watchers_; // 所有连进来的 Service（推送用）
};
```

### 5.3 协议（TCP frame，沿用 `EServerMessageType` 风格）

新增 `EServiceRegistryMessageType` enum 在 `Common/Net/ServiceDiscovery/RegistryProtocol.h`：

| 枚举成员 | 方向 | Payload |
|---|---|---|
| `Register` | C→S | `FRegistryEndpoint`（ServerId, ServerType, Address, Port, ActorIds[]） |
| `Deregister` | C→S | `uint32 ServerId` |
| `Heartbeat` | C→S | `uint32 ServerId, uint64 TimestampMs` |
| `UpdateActors` | C→S | `uint32 ServerId, TVector<uint64>` |
| `ListEndpoints` | C→S | `EServerType ServerType` → resp: `uint64 Seq, TVector<FRegistryEndpoint>` |
| `EndpointChange` | S→C | `EServerType ServerType, uint64 Seq, TVector<FRegistryEndpoint>` |
| `Ack` | 双向 | `EServiceRegistryResult Status, MString Message` |

```cpp
enum class EServiceRegistryMessageType : uint8_t {
    Register       = 200,
    Deregister     = 201,
    Heartbeat      = 202,
    UpdateActors   = 203,
    ListEndpoints  = 204,
    EndpointChange = 205,
    Ack            = 206,
};
```

**base 200 起跳**：避开 `EServerMessageType` 已用的 `MT_RPC=27 / MT_FunctionCall=28 / MT_FunctionResponse=29` 区间。Wire 1-byte type field 直接对应此枚举值。

`MEndpointCache` 启动期主动发 `ListEndpoints` 拉全量；后续被动接收 `EndpointChange`。

### 5.4 心跳与状态机

| 事件 | 阈值 | 行为 |
|---|---|---|
| 心跳超时（无 Heartbeat 包） | 15s（3 × 周期） | `bHealthy=false` + 推 `EndpointChange` |
| 完全失联 | 30s（6 × 周期） | 从 `Endpoints_` 移除 + 推 `EndpointChange` |
| 周期内收到心跳 | 5s | 续约 `LastHeartbeatMs`，无事件 |
| Service 重连进 Registry | < 重连后立即 | `Register` 重新注册（覆盖），推 `EndpointChange` |

`MServiceRegistry::Tick` 每帧检查所有 endpoint 的 `LastHeartbeatMs` 距今差值。

### 5.5 错误处理（DEBUG vs RELEASE）

通过新增的 `MLogger::IsDebugBuild()` 静态函数（基于 `NDEBUG` 宏）切换：

| 场景 | DEBUG | RELEASE |
|---|---|---|
| Registry listen 失败 | `LOG_FATAL` 退出 | `LOG_FATAL` 退出（两个模式都是 FATAL——进程起不来什么都不能做） |
| `Register` 失败（重复 ServerId 不一致） | `LOG_FATAL`（protocol bug） | `LOG_ERROR` 拒绝覆盖 |
| 心跳 timeout 移除 endpoint | `LOG_INFO`（DEV 可见） | `LOG_INFO` 一次 + `LOG_WARN` 之后每次 unhealthy 翻转 |
| 推送 EndpointChange 失败（连接断开） | `LOG_WARN` | `LOG_WARN` |

## 6. MEndpointCache（Service 侧）

### 6.1 接口

```cpp
// Common/Net/ServiceDiscovery/EndpointCache.h
class MEndpointCache {
public:
    static MEndpointCache& Get();

    // 启动期
    void BindRegistry(const MString& Addr, uint16 Port);   // 连 Registry
    void RegisterLocal(const FServiceEndpoint& Self);       // 注册本 Service

    // 业务期
    TSharedPtr<MServerConnection> GetOrConnect(EServerType TargetServerType);

    // Registry push 回调
    void OnEndpointChange(EServerType ServerType, uint64 Seq, const TVector<FServiceEndpoint>& NewEndpoints);

    // 关闭期
    void DeregisterAndShutdown();

    // 主循环每帧调
    void Tick(float DeltaTime);

private:
    TSharedPtr<MTcpConnection> RegistryConn_;
    bool bRegistryConnected_ = false;
    float RegistryReconnectTimer_ = 0.0f;
    TSet<EServerType> PendingListTypes_;       // 重连后待 ListEndpoints 的类型

    TMap<EServerType, TVector<FServiceEndpoint>> Endpoints_;
    TMap<uint64, TSharedPtr<MServerConnection>> ConnectionPool_;  // by ServerId
    TMap<EServerType, uint64> LastSeq_;

    uint32 LocalServerId_ = 0;
    EServerType LocalServerType_ = EServerType::Unknown;
};
```

### 6.2 `GetOrConnect` 流程

```
GetOrConnect(TargetType):
  1. 查 Endpoints_[TargetType]
  2. 空 → log warn (DEBUG) / log debug (RELEASE), return nullptr
  3. filter bHealthy，按 round-robin 选一个 endpoint
  4. 空 healthy → log warn, return nullptr
  5. 用 ServerId 查 ConnectionPool_
  6. 命中且 IsConnected() → return
  7. 未命中或断线 → new MServerConnection(Config), Connect()
  8. 成功 → 存 ConnectionPool_, return
  9. 失败 → 标 endpoint bHealthy=false, return nullptr
```

### 6.3 `OnEndpointChange` 增量更新

```
OnEndpointChange(ServerType, Seq, NewEndpoints):
  if Seq < LastSeq_[ServerType]:
    if MLogger::IsDebugBuild():
      LOG_FATAL("stale push: ServerType=%d Seq=%llu LastSeq=%llu")  // 协议 bug
    else:
      LOG_WARN(...)  // 降级：拒绝覆盖，宁可暂时没最新数据
    return
  LastSeq_[ServerType] = Seq

  for ep in NewEndpoints:
    if ep in Endpoints_[ServerType]:
      update existing entry's bHealthy + ActorIds
    else:
      add to Endpoints_[ServerType]  // 不主动连，留给 GetOrConnect lazy

  // 处理消失的 endpoint
  for existing in Endpoints_[ServerType]:
    if existing.ServerId not in NewEndpoints:
      existing.bHealthy = false
      // 不立即断开——保留 connection 让其 Tick 自愈
      // 下次 OnEndpointChange 该 ServerId 又出现 → bHealthy=true → 复用连接
```

### 6.4 Registry 断线 / 重连

```
RegistryConn_ 断线事件:
  log warn "registry disconnected"
  bRegistryConnected_ = false
  RegistryReconnectTimer_ = 0
  Endpoints_ 保持——GetOrConnect 仍能选 endpoint 发 RPC（连接降级路径）

Tick:
  if !bRegistryConnected_:
    RegistryReconnectTimer_ += DeltaTime
    if RegistryReconnectTimer_ >= 5.0f:
      尝试 BindRegistry
      if success: bRegistryConnected_ = true
        for Type in PendingListTypes_: 主动发 ListEndpoints 重建缓存
        log info "registry reconnected"
```

### 6.5 错误处理（DEBUG vs RELEASE）

| 场景 | DEBUG | RELEASE |
|---|---|---|
| `BindRegistry` TCP 连接失败 | `LOG_FATAL` 退出（明确报错，iteration 快） | `LOG_ERROR` + `Endpoints_` 保持空 + 后台重连 + `GetOrConnect` 返 `nullptr`（调用方 retry） |
| `RegisterLocal` 失败（Registry 拒绝） | `LOG_FATAL`（protocol bug） | `LOG_ERROR` + 继续跑（其他 Service 不知道你存在，但本地 RPC 仍可工作） |
| `ListEndpoints` 响应失败 | `LOG_FATAL` | `LOG_ERROR` + 空表启动 + 后台重试 |
| `OnEndpointChange` Seq 旧 | `LOG_FATAL`（protocol bug） | `LOG_WARN` 拒绝覆盖 |
| `GetOrConnect` 返 nullptr | `LOG_WARN` | `LOG_DEBUG`（避免 RPC 失败风暴刷屏） |
| Registry 重连成功 | `LOG_INFO` | `LOG_INFO` |

## 7. 调用点改动

| 文件 | 改动 |
|---|---|
| `Source/Servers/EchoService/EchoService.h` | `SEchoServiceConfig` 删 `Peers`；加 `MPROPERTY(Meta=(Cli="--registry"))` `MString RegistryAddr` |
| `Source/Servers/Gateway/GatewayServer.h` | 同上 `SGatewayConfig` |
| `Source/Servers/EchoService/EchoService.cpp` | 删 `ConnectAllPeers()`；在 `Init` 末尾加 `MEndpointCache::Get().BindRegistry(Config.RegistryAddr); MEndpointCache::Get().RegisterLocal(ToEndpoint(Config));` |
| `Source/Servers/Gateway/GatewayServer.cpp` | 同上 |
| `Source/Servers/EchoService/EchoService.cpp` | `ShutdownConnections` 加 `MEndpointCache::Get().DeregisterAndShutdown()`；删 `ClearRpcTransports()` |
| `Source/Servers/Gateway/GatewayServer.cpp` | 同上 |
| `Source/Servers/EchoService/EchoService.cpp` | `TickBackends` 加 `MEndpointCache::Get().Tick(0.0f);` |
| `Source/Servers/Gateway/GatewayServer.cpp` | 同上 |
| `Source/Common/Net/Rpc/RpcRuntimeContext.h` | `MServerRuntimeContext::ResolveServerTransport` 改为 `return MEndpointCache::Get().GetOrConnect(Type)`；`RegisterRpcTransport / UnregisterRpcTransport / ClearRpcTransports / GetRpcTransports / RpcTransports` 标记 `[[deprecated]]` 或删掉（PoC 阶段：直接删） |
| `Source/Common/Runtime/Log/Logger.h` | 新增 `static bool MLogger::IsDebugBuild()` 返回 `!defined(NDEBUG)` |
| `CMakeLists.txt` | 新增 `mession_add_service(MServiceRegistry MServiceRegistry SServiceRegistryConfig Source/Servers/ServiceRegistry/ServiceRegistryMain.cpp ...)`；新增 `Source/Servers/ServiceRegistry/ServiceRegistryMain.cpp` 等 |

**`MEndpointCache::ToEndpoint` 构造器**（Helper）：

```cpp
FServiceEndpoint ToEndpoint(const SEchoServiceConfig& Config) {
    FServiceEndpoint Ep;
    Ep.ServerType = Config.LocalServerType;
    Ep.ServerId = Config.LocalServerId;
    Ep.Address = "0.0.0.0";           // PoC 同主机 loopback
    Ep.Port = Config.ListenPort;
    for (uint32 InstId : Config.LocalActorIds) {
        Ep.ActorIds.push_back(MServiceId::Make(Config.LocalServerType, InstId));
    }
    return Ep;
}
```

`Address="0.0.0.0"` + `Port=Config.ListenPort`——PoC 阶段 Registry 不解析 IP，由 Service 自己 `Config.ListenPort` + loopback 通；跨主机写在 follow-up spec。

## 8. Registry 进程 binary

新增 `Source/Servers/ServiceRegistry/`：

```
ServiceRegistry/
├── ServiceRegistryMain.cpp    // mession_add_service 注册 main
├── ServiceRegistryServer.h    // TCP server + 协议分发
├── ServiceRegistryServer.cpp
└── ServiceRegistryConfig.h    // SServiceRegistryConfig { ListenPort, HeartbeatIntervalMs, ... }
```

`mession_add_service` 在 CMakeLists.txt 注册——同 `GatewayServer` / `EchoService` 模板：

```cmake
mession_add_service(MServiceRegistry MServiceRegistry SServiceRegistryConfig Source/Servers/ServiceRegistry/ServiceRegistryMain.cpp
    SOURCES
        Source/Servers/ServiceRegistry/ServiceRegistryServer.cpp
    GENERATED_GROUPS shared registry
    GENERATED_SOURCES
)
```

**ServiceRegistryMain.cpp**：

```cpp
#include "Servers/ServiceRegistry/ServiceRegistryServer.h"
#include "Servers/ServiceRegistry/ServiceRegistryConfig.h"
#include "Servers/App/ServiceMain.h"

int main(int argc, char** argv)
{
    return MServiceMain::Run<MServiceRegistry, SServiceRegistryConfig>(argc, argv);
}
```

**`MServiceRegistry` 类**：

```cpp
MCLASS(Type=Service)
class MServiceRegistry : public MNetServerBase, public MObject {
public:
    MGENERATED_BODY(MServiceRegistry, MObject, 0)
    using MObject::Tick;

    bool Init(int InPort = 0);
    void Tick() override;
    void TickBackends() override;
    void ShutdownConnections() override;
    void OnRunStarted() override;

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;

    void RegisterLocal(const FServiceEndpoint& Endpoint);

private:
    SServiceRegistryConfig Config;
    TMap<uint32, FServiceEndpoint> Endpoints_;
    TMap<uint64, TSharedPtr<MTcpConnection>> Watchers_;  // by ServerId
    TMap<EServerType, uint64> MonotonicSeq_;
    void TickHeartbeats();
    void BroadcastEndpointChange(EServerType ServerType);
    void SendEndpointListTo(uint32 ServerId, EServerType Type);
};
```

## 9. 包格式

沿用 `MServerConnection` 现有 `1 byte type + 1 byte reserved + length-prefixed payload` 格式。`EServiceRegistryMessageType` 新增类型（§5.3 enum，base 200）——**复用同 envelope**，不新增 envelope 类型。

`FRegistryEndpoint` wire 格式：

```
uint32 ServerId
EServerType ServerType (uint32)
uint16 AddrLen + Addr bytes
uint16 Port
uint16 NumActors + uint64[] ActorIds
```

## 10. 测试

### 10.1 validate.py 新增 `service_registry` suite（RELEASE 模式跑）

1. 启动 `MServiceRegistry --listen=18000`
2. 启动 `EchoService_1 --registry=127.0.0.1:18000 --listen=17001 --inst=1 --actors=1001`
3. 启动 `EchoService_2 --registry=127.0.0.1:18000 --listen=17002 --inst=2 --actors=2001`
4. 5s 后：从 EchoService_2 内通过 `MEndpointCache::Get().GetOrConnect(EServerType::Echo)` 拿到 `EchoService_1` 的连接（pool 内 lazy 创建）
5. kill `EchoService_1`，35s 后：`EchoService_2` 收到 `EndpointChange`，`EchoService_1` 端点从 `Endpoints_[Echo]` 移除
6. 启动 `Gateway --registry=127.0.0.1:18000 --listen=18001` —— Gateway `GetOrConnect(EServerType::Echo)` 拿到 2 个连接（懒建）

### 10.2 DEBUG-only suite（DEBUG 模式跑）

`service_registry_debug`：

1. 启动完整 stack
2. **人工注入** `EServiceRegistryMessageType::EndpointChange` 包带旧 Seq → 期望 `LOG_FATAL` 触发 abort
3. CI 用 DEBUG 跑这条；RELEASE 跑 release 用 `LOG_WARN` 拒绝覆盖

### 10.3 手动 sanity check（不进 CI）

```
$ ./Bin/MServiceRegistry --listen=18000 &
$ ./Bin/EchoService --registry=127.0.0.1:18000 --listen=17001 --inst=1 --actors=1001 --local-type=Echo &
$ ./Bin/Gateway --registry=127.0.0.1:18000 --listen=18001 --local-type=Gateway &
# EchoService 启动 5s 后应看到：
#   [INFO] MServiceRegistry: Echo@127.0.0.1:17001 healthy
# Gateway 启动后：
#   [INFO] MEndpointCache: lazy connect Echo@127.0.0.1:17001
#   [INFO] MEndpointCache: lazy connect Echo@127.0.0.1:17001 (already pooled)
```

## 11. 实施分块（给 writing-plans 用）

1. **`MLogger::IsDebugBuild()` + 测试**（1 个 commit）
2. **`Common/Net/ServiceDiscovery/Endpoint.h` + `RegistryProtocol.h`** —— 数据结构 + 包格式常量（1 个 commit）
3. **`MServiceRegistry` 进程实现**（TCP server + Register/Deregister/Heartbeat/UpdateActors/ListEndpoints + Tick 心跳超时 + EndpointChange 推送 + DEBUG/RELEASE 错误分支）（2-3 个 commit）
4. **`mession_add_service` 注册 MServiceRegistry**（1 个 commit）
5. **`MEndpointCache`**（BindRegistry / RegisterLocal / GetOrConnect / OnEndpointChange / Tick / DEBUG/RELEASE 错误分支）（2-3 个 commit）
6. **`SEchoServiceConfig` / `SGatewayConfig` 加 `--registry` 删 `--peers`**（1 个 commit）
7. **`EchoService::Init` / `Gateway::Init` 接 MEndpointCache**（删 `ConnectAllPeers`）（1 个 commit）
8. **`MServerRuntimeContext::ResolveServerTransport` 改走 MEndpointCache**（1 个 commit）
9. **`MRpcChannel::SendToActor` 适配新路径**（如果之前直接走 RpcTransports map，改为走 Resolver；通常 Resolver 不变就自动适配）（1 个 commit）
10. **validate.py `service_registry` suite**（1 个 commit）

每个 commit 单测 / 集成测都过一遍。

## 12. 风险

- **`Address="0.0.0.0"`**：PoC 同主机 OK，跨主机 ERR_CONNREFUSED。follow-up spec 解决（K8s service / DNS）。
- **Registry 单点**：进程挂了 Service 降级跑（RELEASE）或 FATAL（DEBUG）。HA / 副本 follow-up。
- **推送乱序**：DEBUG FATAL，RELEASE 拒绝覆盖——后者可能"陈旧 endpoint 持续可见"。可接受。
- **`EServiceRegistryMessageType` 跟 `EServerMessageType` 走同一 envelope**（1 byte type field）——Service 必须先识别 Registry 类型才能 fallback 到 RPC。需要 `MServerConnection` 在 `ProcessRecv` 处分发到 `MEndpointCache` 而不是 RPC dispatch。
