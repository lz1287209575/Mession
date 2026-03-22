# TODO

这份文档只记录当前 `Source/Servers` 重写的核心决议、已完成项和下一阶段收口项。

## 当前架构决议

- 服务器应用层统一走反射 RPC
- 客户端入口统一为 `ClientCall`
- 跨服入口统一为 `ServerCall`
- 有 `Request` 就必须有 `Response`
- 应用层目标返回形态统一收敛到 `MFuture<TResult<Response, FAppError>>`
- 不再保留旧的手工消息注册、消息 traits、客户端 API / 跨服 API 定义层

## 当前状态

### 已完成

#### 1. 重建 `Source/Servers` 骨架

- `Gateway / Login / World / Scene / Router / Mgo` 已按新目录结构重建
- 服务启动入口统一收敛到 `Source/Servers/App/ServerMain.h`
- 旧 `WorldServer` 下的大量历史实现已移除，避免继续在旧结构上打补丁

#### 2. 纯反射 RPC 基础链路已落地

- `Gateway` 负责客户端 `MT_FunctionCall` 入站
- 后端服务统一处理 `MT_FunctionCall`
- 后端响应统一通过反射 RPC runtime 回包
- 公共分发辅助统一放在 `Source/Servers/App/ServerRpcSupport.h`

#### 3. 新应用层拆分已建立

- `GatewayServer` 只保留连接、runtime、分发壳职责
- 客户端编排下沉到 `Source/Servers/App/GatewayClientFlows.h`
- 登录会话逻辑下沉到 `Source/Servers/App/LoginSessionService.h`
- 世界玩家逻辑下沉到 `Source/Servers/App/WorldPlayerService.h`
- 场景会话逻辑下沉到 `Source/Servers/App/SceneSessionService.h`
- 路由注册逻辑下沉到 `Source/Servers/App/RouterRegistryService.h`
- 持久化玩家逻辑下沉到 `Source/Servers/App/MgoPlayerStateService.h`

#### 4. 第一版协议骨架已补齐

- 应用错误与通用消息：`Source/Protocol/Messages/AppMessages.h`
- 客户端调用消息：`Source/Protocol/Messages/ClientCallMessages.h`
- 世界玩家消息：`Source/Protocol/Messages/WorldPlayerMessages.h`
- 后端服务消息：`Source/Protocol/Messages/BackendServiceMessages.h`
- 客户端下行能力承载：`Source/Common/Net/ClientDownlink.h`

#### 5. 当前已经打通的骨架能力

- `Gateway` 客户端入口：
- `Client_Login`
- `Client_FindPlayer`
- `Client_Logout`
- `Client_SwitchScene`
- `World` 服务入口：
- `PlayerEnterWorld`
- `PlayerFind`
- `PlayerUpdateRoute`
- `PlayerLogout`
- `PlayerSwitchScene`
- `Scene` 服务入口：
- `EnterScene`
- `LeaveScene`
- `Router` 服务入口：
- `ResolvePlayerRoute`
- `UpsertPlayerRoute`
- `Mgo` 服务入口：
- `LoadPlayer`
- `SavePlayer`

## 下一阶段重点

### A. 串一条完整应用链路

目标：

- 先把“登录入世界”链路按新骨架完整串起来
- 路径建议：
- `Gateway.Client_Login`
- `Login.ServerCall`
- `World.PlayerEnterWorld`
- `Router.UpsertPlayerRoute`
- 必要时接 `Mgo.LoadPlayer`

完成标准：

- 可以明确看到一条纯反射 RPC 的端到端编排链
- 中间不再回落到旧消息处理模型

### B. 继续收口 `Gateway` 壳层

当前问题：

- `GatewayServer` 虽然已经明显变薄，但仍有一部分应用编排痕迹

目标方案：

- 继续把非连接管理、非 runtime、非反射分发的逻辑下沉到 flow / service
- 让 `GatewayServer` 只承担网关适配器职责

完成标准：

- `GatewayServer.cpp` 主要只剩：
- 连接生命周期
- runtime 分发
- 依赖组装

### C. 抽公共 server 壳模式

当前问题：

- 六个 server 里仍存在重复的：
- `OnAccept`
- `ShutdownConnections`
- `HandlePeerPacket`
- 本地 peer 连接表管理

目标方案：

- 提炼公共 helper 或公共基类
- 统一后端 peer 注册、分发、关闭流程

完成标准：

- 各 server 只保留自身差异化职责
- 重复样板代码明显下降

### D. 明确 Future / 协程风格

当前问题：

- 目前已统一到 `MFuture<TResult<...>>` 方向，但跨服编排仍是骨架态

目标方案：

- 在应用层正式约定跨服调用组合方式
- 优先按 `MFuture` 风格推进，后续再视情况补 `MCoroutine`

完成标准：

- 应用层链路不再出现同步拼装式代码
- 错误返回统一走 `FAppError`

## 暂不回收的旧议题

下面这些方向仍然成立，但暂时不作为当前阶段主线：

- `MObject / GC / Outer / RootSet`
- `NewMObject / CreateDefaultSubObject`
- `MPlayerAvatar / MPlayerSession` 的正式对象树化
- Avatar member 全量纳入默认子对象体系

原因：

- 当前优先级是先把 Server 层新骨架立稳
- 等服务边界、调用模型、应用编排方式稳定后，再回头设计运行时对象系统

## 推荐推进顺序

1. 先打通“登录入世界”主链路
2. 再继续压缩 `Gateway / Login / World` 的壳层职责
3. 再抽六个 server 的公共样板
4. 最后再回到对象系统与玩家运行时模型重构
