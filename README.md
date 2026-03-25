# Mession

Mession 是一个基于 C++20 的多服游戏服务端实验工程。当前仓库已经完成从“按服务器堆逻辑”到“按运行时 / 协议 / 服务 / 领域对象分层”的重构，代码主线围绕以下几件事展开：

- 统一反射系统：`MCLASS` / `MSTRUCT` / `MPROPERTY` / `MFUNCTION`
- 统一 RPC 调用链：客户端调用、服间调用、负载编解码、运行时路由
- 统一对象状态流：对象属性脏标记、Persistence、Replication 共用同一套对象域快照能力
- 统一服务结构：`Server -> Rpc -> ServiceEndpoint -> Domain Object`

仓库当前重点不是展示单个玩法，而是提供一套可以继续扩展的服务端骨架。

## 仓库地图

- `Source/Common`
  运行时基础设施，包括网络、事件循环、并发、反射、对象系统、日志、持久化、复制。
- `Source/Protocol`
  所有跨进程消息与协议定义，已经按业务域拆分到 `Messages/*`。
- `Source/Servers`
  各个服务器实现，当前包含 `Gateway / Login / World / Scene / Router / Mgo`。
- `Source/Tools`
  工具程序，当前主要是 `MHeaderTool` 和 `NetBench`。
- `Scripts`
  本地启动、验证、协议检查等脚本。
- `Docs`
  正式文档目录。
- `Build`
  CMake 构建目录，同时承载 `Build/Generated` 反射生成结果。
- `Bin`
  所有可执行文件输出目录。

## 当前服务拓扑

- `GatewayServer` `8001`
  客户端入口，处理 `Client_*` 调用，并把请求转发给 `Login` / `World`。
- `LoginServer` `8002`
  登录会话签发与校验。
- `WorldServer` `8003`
  玩家主状态归属、对象树维护、持久化脏数据提交、路由与切场协作。
- `SceneServer` `8004`
  场景进入/离开与轻量场景态。
- `RouterServer` `8005`
  玩家路由注册与查询。
- `MgoServer` `8006`
  玩家持久化记录加载与保存，当前可工作在内存模式，亦可选接 Mongo。

## 典型调用链

1. 客户端连接 `GatewayServer`
2. 通过统一 `MT_FunctionCall` 发起 `Client_Login`
3. `Gateway` 转调 `Login.IssueSession`
4. `Gateway` 或 `World` 继续通过服间 RPC 与 `World / Router / Scene / Mgo` 协作
5. `World` 持有 `MPlayerSession` 对象树，并基于脏标记驱动持久化与复制

## 快速开始

1. 配置构建：

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
```

2. 编译：

```bash
cmake --build Build -j4
```

3. 一键起服：

```bash
python3 Scripts/servers.py start --build-dir Build
```

4. 跑最小链路验证：

```bash
python3 Scripts/validate.py --build-dir Build --no-build
```

5. 停服：

```bash
python3 Scripts/servers.py stop --build-dir Build
```

## 文档入口

- [Docs/README.md](/root/Mession/Docs/README.md)
- [Docs/Architecture.md](/root/Mession/Docs/Architecture.md)
- [Docs/BuildAndRun.md](/root/Mession/Docs/BuildAndRun.md)
- [Docs/RuntimeAndRpc.md](/root/Mession/Docs/RuntimeAndRpc.md)
- [Docs/GameplayAndState.md](/root/Mession/Docs/GameplayAndState.md)
- [Docs/PersistenceAndReplication.md](/root/Mession/Docs/PersistenceAndReplication.md)
- [Docs/Tooling.md](/root/Mession/Docs/Tooling.md)
- [Docs/Validation.md](/root/Mession/Docs/Validation.md)
- [Docs/Roadmap.md](/root/Mession/Docs/Roadmap.md)

## 当前实现状态

- 服务骨架、协议目录、RPC 运行时、对象域快照能力已经收敛到同一套结构
- `World` 的玩家主状态对象树已经使用 `MPROPERTY` 域标记驱动 Persistence / Replication
- 持久化记录协议已从手写归档提升为 `MSTRUCT + MPROPERTY` 可反射结构
- `MFuture / MPromise / MCoroutine` 已形成基础分层，但高阶链式编排能力仍有收敛空间

## 阅读建议

如果是第一次接触这个仓库，建议按以下顺序阅读：

1. [Docs/Architecture.md](/root/Mession/Docs/Architecture.md)
2. [Docs/BuildAndRun.md](/root/Mession/Docs/BuildAndRun.md)
3. [Docs/RuntimeAndRpc.md](/root/Mession/Docs/RuntimeAndRpc.md)
4. [Docs/GameplayAndState.md](/root/Mession/Docs/GameplayAndState.md)
5. [Docs/PersistenceAndReplication.md](/root/Mession/Docs/PersistenceAndReplication.md)
