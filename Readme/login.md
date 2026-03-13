# Login 模块（`Source/Servers/Login`）

`LoginServer` 负责登录结果生成与会话校验，是客户端登录闭环中的认证服务。  
客户端不直接连接 `LoginServer`，而是通过 `GatewayServer` 转发登录请求。

## 主要职责

- 接收 `Gateway` 转发的登录请求（`MT_PlayerLogin`）
- 生成 `SessionKey` 并返回登录结果
- 维护在线会话表
- 为 `WorldServer` 提供 `SessionKey` 校验能力
- 向 `RouterServer` 注册自身信息，供其他服务发现

## 消息职责

### 登录

- 输入：`MT_PlayerLogin`
- 输出：`MT_PlayerLogin` 响应负载，包含至少：
  - `PlayerId`
  - `SessionKey`

`Gateway` 根据这个结果构造客户端侧的 `MT_LoginResponse(SessionKey, PlayerId)`。

### 会话校验

- 输入：`MT_SessionValidateRequest`
- 输出：`MT_SessionValidateResponse`

`WorldServer` 在把玩家正式加入世界前，必须向 `LoginServer` 校验 `SessionKey`，避免绕过认证直接进入世界。

## 会话状态

`LoginServer` 内部维护核心映射：

- `SessionKey -> Session`
- `PlayerId -> SessionKey`

会话具备过期策略，通常会在定期 `Tick()` 中清理失效会话。

## 与 Router 的关系

- 启动时向 `RouterServer` 注册自身（`MT_ServerRegister`）
- 被其他服务（特别是 `Gateway` 和 `World`）通过 Router 发现
- 自身不依赖 Router 做业务路由查询，更多扮演“被发现的认证服务”角色

## 设计边界

`LoginServer` 不负责：

- 玩家选服（交给 Router）
- 世界主状态与场景管理（交给 World/Scene）
- 客户端长连接维护（交给 Gateway）

它专注于：**账号/会话层的正确性**。  
