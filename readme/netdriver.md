# NetDriver 模块

## 作用

`NetDriver/` 负责项目里的运行时网络对象与复制系统，主要包括：

- `NetObject.h`
- `Replicate.h`
- `ReplicationDriver.h`

这一层服务于 `WorldServer`，用于把服务端对象状态同步给连接上的客户端。

## 核心对象

### `MObject / MActor`

这是运行时对象体系的基础：

- `MObject` 提供对象身份和基础能力
- `MActor` 表示可参与世界同步的实体

在当前项目中，玩家进入世界后会关联一个 `MActor` 作为角色实体。

### `MReplicationDriver`

复制驱动负责：

- 注册需要复制的 `Actor`
- 按连接维护相关对象集合
- 生成 `ActorCreate / ActorDestroy / ActorUpdate`
- 把复制消息通过连接下发给客户端

## 当前复制链路

在当前实现里：

1. `WorldServer` 创建玩家角色 `MActor`
2. 注册到 `MReplicationDriver`
3. 给相关连接登记可见对象
4. 由复制驱动生成客户端消息并发送

## 当前协议依赖

`NetDriver` 当前直接使用客户端消息枚举中的这些类型：

- `EClientMessageType::MT_ActorCreate`
- `EClientMessageType::MT_ActorDestroy`
- `EClientMessageType::MT_ActorUpdate`

## 设计边界

`NetDriver` 目前只负责“对象如何同步”，不负责：

- 玩家如何登录
- 世界服如何选路
- 场景服如何发现
- AOI 如何裁剪

这些应由 `WorldServer` 和更上层系统决定。

## 后续可演进方向

- 接入 `AOIComponent`
- 规范复制包格式
- 增加脏数据聚合和批量同步
- 细化可见性和频率控制
