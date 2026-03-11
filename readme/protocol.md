# 协议总览

## 统一包格式

项目当前统一使用：

- `Length(4) + MsgType(1) + Payload(N)`

其中：

- `Length` 表示后续消息体总长度
- `MsgType` 表示消息类型
- `Payload` 表示业务负载

## 客户端协议

客户端侧消息类型由 `EClientMessageType` 定义。

当前主链路常用消息：

| 消息 | 值 | 说明 |
|------|----|------|
| `MT_Login` | `1` | 客户端登录请求 |
| `MT_LoginResponse` | `2` | 登录响应 |
| `MT_PlayerMove` | `5` | 玩家移动输入 |
| `MT_ActorCreate` | `6` | 创建 Actor |
| `MT_ActorDestroy` | `7` | 销毁 Actor |
| `MT_ActorUpdate` | `8` | Actor 属性同步 |

## 跨服协议

跨服消息由 `EServerMessageType` 定义。

### 基础后端消息

- `MT_ServerHandshake`
- `MT_ServerHandshakeAck`
- `MT_Heartbeat`
- `MT_HeartbeatAck`

### 业务跨服消息

- `MT_PlayerLogin`
- `MT_PlayerLogout`
- `MT_PlayerSwitchServer`
- `MT_PlayerDataSync`
- `MT_SessionValidateRequest`
- `MT_SessionValidateResponse`

### Router 控制面消息

- `MT_ServerRegister`
- `MT_ServerRegisterAck`
- `MT_ServerUnregister`
- `MT_ServerLoadReport`
- `MT_RouteQuery`
- `MT_RouteResponse`

## 当前关键负载

### 登录响应

- `MsgType(1)`
- `SessionKey(4)`
- `PlayerId(8)`

### 后端握手

- `ServerId(4)`
- `ServerType(1)`
- `NameLen(2)`
- `ServerName`

### Router 注册

- `ServerId(4)`
- `ServerType(1)`
- `NameLen(2) + ServerName`
- `AddrLen(2) + Address`
- `Port(2)`

### Router 路由查询

- `RequestId(8)`
- `ServerType(1)`
- `PlayerId(8)`

当前约定：

- `PlayerId = 0` 表示普通服务发现
- `PlayerId != 0` 表示按玩家做世界服稳定路由

## 协议分层建议

为了避免后续继续混乱，建议保持三层边界：

- `Core/Socket`: 只负责完整包收发
- `Common/ServerConnection`: 只负责后端握手、心跳和公共消息入口
- 业务服务模块：负责具体消息语义和负载解释
