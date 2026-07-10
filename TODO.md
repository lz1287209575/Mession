# TODO

这份文档记录当前这一轮 PoC 收口之后，最值得继续推进的近期待办。

更新时间基线：

- 当前内容已对齐到 `2026-07-09` 这次"Gateway + 同质多进程 EchoService" 收口之后的仓库状态
- 已确认 `cmake --build Build -j4` 可通过
- 已确认 `Scripts/validate.py --build-dir Build --no-build` 跑链路 1 + 链路 2 + 错误链路通过
- 历史 6 服（Login/World/Scene/Router/Mgo）以及 `Player/Controller/Pawn/Profile/Inventory/Progression/CombatProfile` 业务代码已经在 PoC 重构中删除，**不恢复**

## 当前已稳定

- 反射系统：`MCLASS` / `MSTRUCT` / `MPROPERTY` / `MFUNCTION` 全部经 `MHeaderTool` 生成 `.mgenerated.cpp/.h` 落到 `Build/Generated/`
- 跨进程 RPC 三入口统一为 `MRpcChannel::Call / CallToActor / SendToClient`，`RpcServerCall.h` 用稳定 FunctionId
- 对象系统：`MObject` + `TSharedPtr` + `IDisposable`，`NewMObject<T>` 模板返回 `TSharedPtr`，业务侧零关心释放
- 同质多进程：`EchoService` 用启动参数 `--inst / --actors` 决定本进程 ActorIds；`MActorRouter` 做进程内寻址
- 服务运行时骨架：`MNetServerBase` 提供统一事件循环 + Listener + 客户端/后端连接管理
- `Scripts/servers.py` + `Scripts/validate.py` 切到 2 服拓扑（`GatewayServer` + 2 个 `EchoService`）
- 旧的 `legacy_runtime.py / compat_protocol_schema.json / ClientFunctionRoute.h` 已清理

## 当前最优先

### 1. 跑通跨进程链 2 真实场景

当前 `EchoService::Echo` 走 `MRpcChannel::CallToActor`，链路 2 已经在客户端能收到回包，但 `MActorRouter` 只在本进程注册 Actor——EchoService_A 不知道 EchoService_B 上有 Actor 2001/2002。

目标：

- 让 `EchoService` 在收到对端连接（`RegisterRpcTransport` 之后）通过某种控制面协议（`MT_FunctionCall` 一个 `RegisterRemoteActors` ServerCall）把对端的 ActorIds 同步到本进程 `MActorRouter`
- `EchoService::Echo` 命中本机 Actor 时走 `IsActorLocal` 短路直接返，否则走 `CallToActor`

### 2. 落实 MClientManifest 真生成

当前 `MClientManifest.generated.h` 是空 stub，`ClientManifest.h` 的 5 个 wrapper 都返回 nullptr。

目标：

- `MHeaderTool` 扫描 `MFUNCTION(Client)` / `MFUNCTION(ClientCall)`，emit 到 `Build/Generated/MClientManifest.mgenerated.cpp`
- `GatewayServer::HandleClientPacket` 用 `MClientManifest::FindByFunctionId` 替代硬编码的 `MClientFunctionRouteTable`

### 3. 补一组真实协议样例

当前 `Protocol/Messages/` 只有 7 个文件，最具业务语义的 `FSampleEchoRequest/Response` 之外一片空白。

目标：

- 至少补 3 个：`FCastSkillRequest/Response`（战斗）、`FQuerySceneResponse`（场景）、`FEnterSceneNotify`（下行通知）
- 给 `MRpcChannel::SendToClient` 一个真实例子

## 第二优先级

### 4. 收口运行时基础层

- `FiberAwait / FiberScheduler / MFUTURE` 当前 Windows backend 是 null 占位，调用 `MAwait` 会抛 `fiber_backend_unsupported`
- 所有 service handler 当前走"同步返回 MFUTURE"，没有真挂起；如要真用 fiber，需要选 `libco` / `boost::context` 替代 deprecated 的 ucontext

### 5. 拆分 ReflectionPropertyTemplates.inl

`Source/Common/Runtime/Reflect/ReflectionPropertyTemplates.inl`（1915 行）单一文件同时承担 JSON / Binary / CLI String 三种 exporter/importer。拆三个 inl 让编译失败时定位更快。

## 不在本周期内

- 战斗、Scene、Login、World、Mgo 业务代码 —— 已经在 PoC 范围内被 EchoService 替代
- `MObjectAssetSmokeTool` 重启 —— 当前 `if(FALSE)` 屏蔽
- `server_cluster.py / server_control_api.py / server_manager_tui.py`（共 4516 行 Python 运维）—— 等 2 服 PoC 稳了再统一替换
- 持久化（`MongoPersistenceSink` / `MPersistenceSubsystem`）—— 当前不暴露，等业务需要再启用