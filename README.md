# Mession

Mession 是一个基于 C++20 的多服游戏服务端实验工程。仓库当前的重点是收敛基建骨架：

- 统一反射系统：`MCLASS` / `MSTRUCT` / `MPROPERTY` / `MFUNCTION`
- 统一 RPC 调用链：客户端调用、服间调用、负载编解码、运行时路由（`MRpcChannel::Call / CallToActor / SendToClient`）
- 统一对象系统：`MObject` + `TSharedPtr` + `IDisposable`（C# 风格），让业务侧零关心释放
- 同质多进程 Service：一个 binary 通过启动参数决定 ServiceName + ActorIds，水平扩展

## 当前仓库已经做到什么

PoC 阶段已打通的最短链路：

- 链路 1：UE/Client → Gateway(8001) → EchoService_A(7001) → 回包
- 链路 2：UE/Client → Gateway(8001) → EchoService_A(7001) → EchoService_B(7002) → 回包
- 错误链路：发一个不存在的 ActorId，回包带 `actor_not_found` 错误码

## 仓库地图

- `Source/Common`
  运行时基础设施，包括对象系统、反射、并发、网络、RPC、Persistence、Replication。
- `Source/Protocol`
  所有跨进程协议与消息定义，按业务域拆分到 `Messages/*`。
- `Source/Servers`
  服务器实现。当前 PoC 阶段只有 `Gateway` 与同质多进程 `EchoService`：
  - `GatewayServer`：唯一对外入口，监听 client TCP，把 `Client_*` / `MT_FunctionCall` 转发到目标 Service。
  - `EchoService`：同质多进程 Service（启动参数决定 ActorIds 和监听端口），通过 `MActorRouter + MRpcChannel::CallToActor` 寻址。
- `Source/Tools`
  工具程序，当前主要是 `MHeaderTool` 和 `NetBench`。
- `Scripts`
  本地启动、验证、协议检查脚本。
- `Docs`
  正式文档目录。
- `Build`
  CMake 构建目录，同时承载 `Build/Generated` 反射生成结果。
- `Bin`
  所有可执行文件输出目录。

## 当前服务拓扑（PoC）

PoC 阶段只有两个 binary，所有"业务服"都是同一个 `EchoService` 进程，靠启动参数决定 ServiceName / ActorIds：

| 进程 | 默认端口 | ServiceName | 角色 |
|------|---------|-------------|------|
| `GatewayServer` | 8001 | — | 客户端入口，转发 Client_* 到业务 Service |
| `EchoService` (instance 1) | 7001 | `MEchoService` | 注册 Actor 1001, 1002 |
| `EchoService` (instance 2) | 7002 | `MEchoService` | 注册 Actor 2001, 2002 |

启动命令示例：

```bash
./Bin/EchoService --listen=7001 --inst=1 --actors=1001,1002 \
    --peers=Echo@127.0.0.1:7002 \
    --service=MEchoService
```

ActorId 布局：[ServiceId (高 32 位 = `EServerType::Echo`=7)][InstId (低 32 位)]。

## 快速开始

> **全新检出（没有 `Build/`、没有 `Bin/`）请直接用一键脚本**——它会把裸 cmake 流程里
> 三个容易卡住的地方处理掉：Ninja 常只随 IDE 分发而不在 `PATH`；第一次 configure 需要先编
> 一个宿主 `MHeaderTool`；以及新检出需要**配置两趟**（configure 期的 MHeaderTool 要读到
> `compile_commands.json` 才能把 `*.Async.cpp` 里的 await 业务体解析出来，而该文件是第一趟
> generate 阶段才写出的）。
>
> ```bash
> Scripts/build_from_scratch.sh                  # Release 构建到 Build/
> Scripts/build_from_scratch.sh -t Debug --test  # Debug 构建到 cmake-build-debug/，构建完跑测试
> Scripts/build_from_scratch.sh --fmt-dir DIR    # 离线环境：用本地 fmt 源码跳过 FetchContent
> ```
>
> 依赖：CMake ≥ 3.15、GCC/Clang 编译器、Clang 19 的 CMake 包
> （Debian/Ubuntu：`apt install libclang-19-dev llvm-19-dev`）。

等价的手工流程见下面 1、2 两步；**第一次配置需要连跑两次 `cmake`**（第一趟会报一部分
`*.mgenerated.cpp` 找不到，那是预期内的，第二趟生成物补齐后即通过）。

1. 配置工程

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release   # 新检出需要第二趟
```

2. 编译

```bash
cmake --build Build -j4
```

Windows 下也可以直接使用：

```bat
Scripts\Build.bat Release
```

3. 启动 PoC 三进程（Gateway + 2 个 EchoService）

```bash
python3 Scripts/servers.py start --build-dir Build
```

默认启动顺序：`EchoService@7001` → `EchoService@7002` → `Gateway@8001`。

4. 跑端到端验证（链 1 + 链 2）

```bash
python3 Scripts/validate.py --build-dir Build --no-build
```

5. 停服

```bash
python3 Scripts/servers.py stop --build-dir Build
```

> 当前 `validate.py` 跑的是骨架链路：
> - 链路 1：Client → Gateway → EchoService（直接命中本机 Actor）
> - 链路 2：Client → Gateway → EchoService_A → EchoService_B（跨进程 hop）
> - 错误链路：发一个不存在的 ActorId，校验回包有 `actor_not_found` 错误码

## 文档入口

`Docs/RefactorArchitectureAndRpc.md` 是当前架构权威说明。

## 文档入口（legacy references）

下面这些链接在 PoC 阶段不再更新——它们面向"6 服异质"历史拓扑。当前仓库只有 `GatewayServer + EchoService` 两类进程，结构与代码请直接看 `Source/` + `RefactorArchitectureAndRpc.md`。

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

1. [Docs/RefactorArchitectureAndRpc.md](/root/Mession/Docs/RefactorArchitectureAndRpc.md)
2. [Source/Common/Net/Rpc/MRpcChannel.h](/root/Mession/Source/Common/Net/Rpc/MRpcChannel.h) — RPC 三入口
3. [Source/Common/Net/Routing/ActorRouter.h](/root/Mession/Source/Common/Net/Routing/ActorRouter.h) — Actor 寻址
4. [Source/Servers/EchoService/EchoService.cpp](/root/Mession/Source/Servers/EchoService/EchoService.cpp) — 端到端示例

## 当前开发重点

PoC 阶段的下一个推进方向：

- **跑通链路 2**：Client → Gateway → EchoService_A → EchoService_B。需要 `MActorRouter` 跨进程同步（当前只有本进程 `RegisterActor`）。
- **MClientManifest 真正落地**：让 MHeaderTool 把 `MFUNCTION(Client)` 反射生成全局 FunctionId → (OwnerType, FunctionName, ResponseType) 表，替代现在的硬编码。
- **补一组真实协议样例**：`FCastSkillRequest/Response / FMoveRequest/Response / FQuerySceneResponse`，给 `MRpcChannel::CallToActor` + `MRpcChannel::SendToClient` 真实例子，而不是只剩一个 `FSampleEchoRequest`。
