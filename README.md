# Mession

Mession 是一个基于 C++20 的多服游戏服务端实验工程。仓库当前的重点不是展示单个玩法，而是把下面几条主线收敛成一套可以继续扩展的服务端骨架：

- 统一反射系统：`MCLASS` / `MSTRUCT` / `MPROPERTY` / `MFUNCTION`
- 统一 RPC 调用链：客户端调用、服间调用、负载编解码、运行时路由
- 统一对象状态流：Persistence / Replication 共用对象域快照能力
- 统一服务结构：`Server -> Rpc -> ServiceEndpoint -> Domain Object`
- 统一玩家状态模型：`MPlayer` 对象树承载主状态与业务入口

## 当前仓库已经做到什么

当前主干已经打通了一个可工作的玩家闭环：

- 登录、进世界、查找玩家、切场景、登出
- `Profile / Pawn / Inventory / Progression` 查询入口
- 金币、装备、经验、生命值等写操作
- 双玩家场景同步下行
- 最小战斗链路：`Client_CastSkill`
- 登出后重登的状态恢复验证

这意味着仓库已经从“只搭骨架”进入“可以持续收口状态边界、补验证、扩玩法能力”的阶段。

## 仓库地图

- `Source/Common`
  运行时基础设施，包括对象系统、反射、并发、网络、RPC、Persistence、Replication。
- `Source/Protocol`
  所有跨进程协议与消息定义，按业务域拆分到 `Messages/*`。
- `Source/Servers`
  各个服务器实现，当前包含 `Gateway / Login / World / Scene / Router / Mgo`。
- `Source/Tools`
  工具程序，当前主要是 `MHeaderTool` 和 `NetBench`。
- `Scripts`
  本地启动、验证、协议检查、控制面脚本。
- `Docs`
  正式文档目录。
- `Build`
  CMake 构建目录，同时承载 `Build/Generated` 反射生成结果。
- `Bin`
  所有可执行文件输出目录。

## 当前服务拓扑

- `GatewayServer` `8001`
  客户端入口，处理 `Client_*` 调用并把请求转发给目标服。
- `LoginServer` `8002`
  登录会话签发与校验。
- `WorldServer` `8003`
  玩家主状态归属、对象树维护、流程编排、持久化边界。
- `SceneServer` `8004`
  场景进入/离开、场景同步、轻量战斗运行时。
- `RouterServer` `8005`
  玩家路由注册与查询。
- `MgoServer` `8006`
  玩家持久化记录加载与保存，可跑内存态，也可选接 Mongo。

## 快速开始

1. 配置工程

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
```

2. 编译

```bash
cmake --build Build -j4
```

Windows 下也可以直接使用：

```bat
Scripts\Build.bat Release
```

3. 本地起服

```bash
python3 Scripts/servers.py start --build-dir Build
```

4. 跑完整验证

```bash
python3 Scripts/validate.py --build-dir Build --no-build
```

5. 停服

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
- [Docs/PlayerRpcDevelopment.md](/root/Mession/Docs/PlayerRpcDevelopment.md)
- [Docs/Validation.md](/root/Mession/Docs/Validation.md)
- [Docs/Tooling.md](/root/Mession/Docs/Tooling.md)
- [Docs/Roadmap.md](/root/Mession/Docs/Roadmap.md)

## 推荐阅读顺序

如果是第一次接触这个仓库，建议按以下顺序阅读：

1. [Docs/Architecture.md](/root/Mession/Docs/Architecture.md)
2. [Docs/BuildAndRun.md](/root/Mession/Docs/BuildAndRun.md)
3. [Docs/RuntimeAndRpc.md](/root/Mession/Docs/RuntimeAndRpc.md)
4. [Docs/GameplayAndState.md](/root/Mession/Docs/GameplayAndState.md)
5. [Docs/PlayerRpcDevelopment.md](/root/Mession/Docs/PlayerRpcDevelopment.md)
6. [Docs/Validation.md](/root/Mession/Docs/Validation.md)

## 当前开发重点

当前最值得继续推进的方向主要有三条：

- 收口玩家状态归属，减少 `Profile / Pawn / Progression / CombatProfile` 之间的桥接同步
- 固化自动验证和可观测性，把现有主链路能力沉淀为稳定回归
- 把战斗和技能配置继续往数据驱动方向推进，而不是只停留在内建默认值
