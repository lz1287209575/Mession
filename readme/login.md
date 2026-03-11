# Login 模块

## 作用

`LoginServer` 负责登录结果生成与会话校验。

它是客户端登录闭环里的认证服务，但客户端并不直接连它，而是由 `GatewayServer` 代理接入。

## 主要职责

- 接收 `Gateway` 转发的登录请求
- 生成 `SessionKey`
- 保存在线会话
- 为 `WorldServer` 提供 `SessionKey` 校验
- 向 `RouterServer` 注册自身信息

## 当前消息职责

### 登录

- 输入：`MT_PlayerLogin`
- 输出：`MT_PlayerLogin` 响应负载，包含 `ConnectionId + PlayerId + SessionKey`

### 会话校验

- 输入：`MT_SessionValidateRequest`
- 输出：`MT_SessionValidateResponse`

这条链路保证 `WorldServer` 不会在未校验 `SessionKey` 的情况下直接放玩家进世界。

## 会话数据

当前维护两张核心映射：

- `SessionKey -> Session`
- `PlayerId -> SessionKey`

会话具备过期时间，并在 `Tick()` 中清理。

## 与 Router 的关系

`LoginServer` 当前会连接 `RouterServer` 并注册自己，但不主动依赖 Router 做业务查询。

在整个拓扑里，它更像是：

- 被发现的认证服务
- 为其他服务提供校验能力

## 设计边界

`LoginServer` 不负责：

- 玩家选服
- 世界状态
- 场景管理
- 客户端长连接维护
