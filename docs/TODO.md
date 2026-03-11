# Mession 基础框架 TODO

> 基于代码扫描整理，用于指导后续补齐与重构。

---

## 1. 类型层（NetCore）— STL 封装不全

按项目 STL Wrapping 规则，以下常用类型尚未在 Core 中提供别名或封装：

| 缺失类型 | 对应 STL | 可能用途 | 状态 |
|----------|----------|----------|------|
| `TUnorderedMap` / `THashMap` | `std::unordered_map` | 高频 O(1) 查找 | ✅ 已添加 |
| `TOptional` | `std::optional` | 可选返回值、解析结果 | ✅ 已添加 |
| `TPair` | `std::pair` | 键值对、多返回值 | ✅ 已添加 |

---

## 2. 线程安全

- [x] **MUniqueIdGenerator**：已改为 `std::atomic` 保证线程安全
- [x] **MLogger**：`MinLevel`、`bConsoleOutput` 等已改为 `std::atomic`，支持运行时 `SetMinLevel` 及文件输出

---

## 3. 时间与定时

- [x] 无统一时间抽象（`TTime`、`TDuration` 等）→ 已添加 `MTime`（`GetTimeSeconds`、`SleepSeconds`、`SleepMilliseconds`）
- [x] 各服务器直接用 `std::chrono` 和 `sleep_for` → 已替换为 `MTime::SleepMilliseconds`
- [x] 无统一 Tick 时钟（如 `GetTimeSeconds()`）→ 已提供 `MTime::GetTimeSeconds()`

---

## 4. 模块与构建

- [x] **Game/GameServer.h**、**Main.cpp**：已加入 CMake 作为 GameServer 目标（端口 7777）
- [x] **MServerConnectionManager**：已实现但未被 Gateway/World 等使用（见下方说明）
- [ ] **NetDriver/Reflection.h**：反射系统存在，使用范围有限

**MServerConnectionManager 未使用说明**：`MServerConnectionManager` 提供集中式服务器间连接管理（AddServer/RemoveServer、SendToServer、Broadcast、Tick）。当前各服务器（Gateway/World/Scene）采用分散式连接：通过 Router 下发的路由信息，在 `ApplyRoute` 回调中自行创建并持有 `MTcpConnection`。若未来需要统一重连、心跳、连接池等能力，可考虑将各服务器的连接逻辑迁移到 `MServerConnectionManager`。

---

## 5. 配置与日志

- [x] **Config.h**：已改用 `TIfstream`（NetCore 别名）
- [x] **Logger**：已用 `FString`，支持 `SetMinLevel` 运行时切换、文件输出（`Init(LogPath)`）

---

## 6. 协议与序列化

- [x] **MessageUtils**：已添加 `HostToNetwork`/`NetworkToHost`（NetCore）及 `AppendValueBE`/`ReadValueBE`（MessageUtils），协议可逐步迁移
- [ ] **ParsePayload**：仅返回 bool，无错误码或错误信息；可逐步迁移为 `TResult`
- [x] 无统一 Result/Error 类型 → 已添加 `TResult<T, E>` 与 `TResult<void, E>`（NetCore.h）

---

## 7. AOI 实现

- [ ] **AOIComponent**：`TMap<SAOICell, SAOICell>` 的 key/value 语义不清晰，通常应为「格子 → 对象集合」映射

---

## 8. 架构一致性

- [x] **INetConnection**：Gateway、Router、Login、World、GameServer、ReplicationDriver 均改为 `TSharedPtr<INetConnection>`，接口已补充 `ReceivePacket`/`ProcessRecvBuffer`/`FlushSendBuffer`/`HasPendingSendData`
- [x] 客户端连接统一通过 `INetConnection` 抽象

---

## 建议的补齐顺序

| 优先级 | 项目 | 说明 |
|--------|------|------|
| ~~高~~ | ~~补齐 Core 类型~~ | ✅ 已完成 |
| ~~高~~ | ~~MUniqueIdGenerator 线程安全~~ | ✅ 已完成 |
| ~~中~~ | ~~统一时间抽象~~ | ✅ 已完成 |
| ~~中~~ | ~~清理/接入孤立代码~~ | ✅ GameServer 已加入 CMake |
| ~~中~~ | ~~使用 MServerConnectionManager~~ | ✅ 已文档说明为何不用 |
| 低 | Result/Error 类型 | 统一错误返回方式 |
| 低 | 字节序显式处理 | 跨平台协议兼容 |

---

*最后更新：基于代码扫描*
