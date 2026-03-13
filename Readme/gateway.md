# Gateway 模块（`Source/Servers/Gateway`）

`GatewayServer` 是客户端接入层，目前是系统唯一的客户端入口。  
它**不承载游戏逻辑**，主要职责是：连接接入、协议分发和后端路由。

## 职责概览

- 监听客户端 TCP 连接（端口 8001）
- 解析客户端消息类型（`EClientMessageType`）
- 把登录请求转发给 `LoginServer`
- 把游戏消息转发给 `WorldServer`
- 把登录结果返回给客户端
- 通过 `RouterServer` 完成 `Login` / `World` 的服务发现与选路

## 登录链路

1. 客户端发送 `EClientMessageType::MT_Login`
2. `Gateway` 将其转成后端消息 `MT_PlayerLogin` 转发到 `LoginServer`
3. `LoginServer` 生成 `SessionKey` 并返回
4. `Gateway` 给客户端下发 `MT_LoginResponse(SessionKey, PlayerId)`
5. `Gateway` 通过 `RouterServer` 查询玩家应落在哪个 `WorldServer`
6. `Gateway` 将登录结果同步到目标 `WorldServer`

## 游戏链路

1. 客户端发送移动等游戏消息（例如 `MT_PlayerMove`）
2. `Gateway` 校验客户端已完成登录
3. `Gateway` 将消息封装为 `MT_PlayerClientSync` 转发给 `WorldServer`
4. `WorldServer` 回推的复制包同样通过 `MT_PlayerClientSync` 回到 `Gateway`
5. `Gateway` 按 `PlayerId` 将复制消息下发给对应客户端

## 与 Router 的关系

`GatewayServer` 通过 `RouterServer` 完成两类核心查询：

- `LoginServer` 发现
- `WorldServer` 发现 + 按 `PlayerId` 的世界服绑定

当 `Gateway` 为某个玩家查询世界服时：

- 首次登录：Router 为该玩家选择一个可用 `WorldServer` 并建立绑定
- 之后登录：Router 返回同一个 `WorldServer`，保证**同一玩家稳定落在同一世界服**

## 设计边界

`GatewayServer` 不负责：

- 账号认证与 Session 校验逻辑
- 世界状态与场景管理
- Actor 复制与 AOI
- 选服策略细节（交给 Router）

它关注的是：**对客户端连接进行管理，并把请求可靠地路由到正确的后端服务**。  
