# World 模块（`Source/Servers/World`）

`WorldServer` 是当前系统的核心业务服，负责玩家进入世界后的状态管理。  
它串联了认证、场景同步和复制系统，是主要的运行时状态承载者。

## 主要职责

- 接收 `Gateway` 转发的玩家登录与游戏消息
- 向 `LoginServer` 校验 `SessionKey`
- 在校验成功后把玩家加入世界
- 创建玩家角色实体，并注册到复制系统
- 驱动 `MReplicationDriver` 向客户端同步对象状态
- 把进场、离场、位置变化同步到 `SceneServer`
- 向 `RouterServer` 注册自己并通过 Router 发现 `LoginServer`

## 核心链路

### 玩家进入世界

1. `Gateway -> World`: `MT_PlayerLogin`
2. `World -> Login`: `MT_SessionValidateRequest`
3. `Login -> World`: `MT_SessionValidateResponse`
4. 校验通过后，`World` 创建玩家数据和角色对象
5. `World -> Scene`: `MT_PlayerSwitchServer`，让场景服创建实体视图

### 玩家移动与状态同步

1. `Gateway -> World`: `MT_PlayerClientSync`（包装客户端输入）
2. `World` 更新玩家位置和相关状态
3. `World -> Scene`: `MT_PlayerDataSync`（同步给场景服）
4. `World` 通过 `NetDriver` 为相关连接生成 `MT_ActorUpdate` 等复制消息

## 与 Router 的关系

`WorldServer` 通过 `RouterServer` 完成：

- 向 Router 注册自身（用于服务发现与负载上报）
- 查询 `LoginServer` 地址（避免硬编码）

当有多个 `WorldServer` 存在时，Router 还会结合玩家绑定和负载信息为玩家选路。

## 内部核心对象

- `Players`：以 `PlayerId` 为主键的玩家状态表
- `PendingSessionValidations`：待完成的 Session 校验上下文
- `ReplicationDriver`：`MReplicationDriver` 实例，负责对象复制
- `BackendConnections`：到 `Login`、`Gateway`、`Scene`、`Router` 等的后端长连接

当前 `World` 的主索引已经统一收敛到 `PlayerId`，不再依赖 `Gateway` 本地 `ConnectionId` 做业务路由。

## 设计边界

`WorldServer` 不负责：

- 账号认证结果生成（交给 `LoginServer`）
- 客户端直连接入（交给 `GatewayServer`）
- 全局服务发现与选服策略（交给 `RouterServer`）

它专注于：**世界内业务状态的维护与对外同步**。  
