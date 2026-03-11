# Gateway 模块

## 作用

`GatewayServer` 是客户端接入层，也是当前系统唯一的客户端入口。

它的职责不是承载游戏逻辑，而是做连接接入、协议分发和后端路由。

## 主要职责

- 接收客户端 TCP 连接
- 解析客户端消息类型
- 把登录请求转发给 `LoginServer`
- 把游戏消息转发给 `WorldServer`
- 把登录结果返回给客户端
- 接入 `RouterServer` 做后端发现和路由查询

## 当前链路

### 登录链路

1. 客户端发送 `EClientMessageType::MT_Login`
2. `Gateway` 转发 `MT_PlayerLogin` 到 `Login`
3. `Login` 返回 `SessionKey`
4. `Gateway` 返回 `MT_LoginResponse` 给客户端
5. `Gateway` 向 `RouterServer` 查询玩家对应的 `World`
6. `Gateway` 再把登录结果转发到目标 `World`

### 游戏链路

1. 客户端发送移动等游戏消息
2. `Gateway` 校验客户端已认证
3. `Gateway` 转发 `MT_PlayerDataSync` 到 `World`

## 与 Router 的关系

`Gateway` 当前通过 `RouterServer` 完成两类查询：

- `LoginServer` 发现
- `WorldServer` 发现

并且对世界服查询已经支持：

- 按 `PlayerId` 进行稳定选路

这意味着同一个玩家可以被路由到同一个 `WorldServer`。

## 设计边界

`Gateway` 不负责：

- 账号认证逻辑
- 会话校验
- 玩家进入世界
- 场景同步

这些都交给后端服务处理。
