# World 模块

## 作用

`WorldServer` 是当前系统里的核心业务服，负责玩家进入世界后的状态管理。

它连接了认证、场景同步和复制系统，是目前最主要的运行时状态承载者。

## 主要职责

- 接收 `Gateway` 转发的玩家登录与游戏消息
- 向 `LoginServer` 校验 `SessionKey`
- 在校验成功后把玩家加入世界
- 创建玩家角色 `MActor`
- 驱动 `MReplicationDriver`
- 把进场、离场、位置变化同步到 `SceneServer`
- 向 `RouterServer` 注册自己并动态发现 `LoginServer`

## 当前链路

### 玩家进入世界

1. `Gateway -> World`: `MT_PlayerLogin`
2. `World -> Login`: `MT_SessionValidateRequest`
3. `Login -> World`: `MT_SessionValidateResponse`
4. `World` 创建玩家数据和角色
5. `World -> Scene`: `MT_PlayerSwitchServer`

### 玩家移动

1. `Gateway -> World`: `MT_PlayerDataSync`
2. `World` 更新玩家位置
3. `World -> Scene`: `MT_PlayerDataSync`

## 与 Router 的关系

`WorldServer` 现在已经通过 `RouterServer` 做两件事：

- 向 Router 注册自身
- 通过 Router 查询 `LoginServer` 的地址

这意味着 `World` 不必再固定依赖写死的 `LoginServer` 地址。

## 核心内存对象

- `Players`
- `ConnectionToPlayer`
- `PendingSessionValidations`
- `ReplicationDriver`
- `BackendConnections`

## 设计边界

`WorldServer` 不负责：

- 账号认证结果生成
- 客户端直连接入
- 全局服务发现策略本身

它只负责世界内业务状态和对外同步。
