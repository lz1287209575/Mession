# NetDriver 模块（`Source/NetDriver`）

`Source/NetDriver` 负责运行时网络对象与复制系统，是 `WorldServer` 内部同步玩家状态到客户端的关键模块。

## 文件概览

```text
Source/NetDriver/
  NetObject.h / .cpp
  Replicate.h
  Reflection.h
  ReflectionExample.h
  ReplicationDriver.h / .cpp
```

## NetObject：网络对象基类

文件：`NetObject.h/.cpp`

- 提供网络对象的基础身份与生命周期接口
- 作为复制系统中的“被同步对象”基类

在当前项目中，玩家进入世界后，会在 `WorldServer` 内部绑定到一个具体的网络对象实例（如角色实体），并通过 `NetDriver` 系统参与复制。

## Replicate & Reflection：属性描述

文件：`Replicate.h`, `Reflection.h`, `ReflectionExample.h`

- `Replicate.h`：
  - 定义用于标记“需要复制的字段”的辅助设施
  - 帮助构建属性列表和变更检测
- `Reflection.h`：
  - 提供轻量反射/字段注册机制
  - 支持根据描述自动序列化/反序列化
- `ReflectionExample.h`：
  - 演示如何使用当前反射与复制机制定义一个可同步对象

这些文件合起来，提供了“**如何描述要同步哪些字段**”的能力。

## ReplicationDriver：复制驱动

文件：`ReplicationDriver.h/.cpp`

`MReplicationDriver` 是复制系统的中枢，主要职责：

- 注册需要复制的对象（通常是玩家角色、重要实体）
- 按连接维护可见对象集合
- 计算属性变更，并构造复制包：
  - `MT_ActorCreate`
  - `MT_ActorDestroy`
  - `MT_ActorUpdate`
- 通过底层连接（`MTcpConnection` / `MServerConnection`）下发到客户端/网关

当前复制链路的大致流程：

1. `WorldServer` 创建玩家角色对象
2. 注册到 `MReplicationDriver`
3. 对每个连接登记其可见对象集合
4. 帧更新时由 `MReplicationDriver` 生成相应 ActorCreate/Destroy/Update 消息
5. 通过网关回推给客户端

## 协议依赖

`NetDriver` 直接依赖客户端消息枚举中的复制相关类型：

- `EClientMessageType::MT_ActorCreate`
- `EClientMessageType::MT_ActorDestroy`
- `EClientMessageType::MT_ActorUpdate`

这些消息的负载格式在协议文档和代码中统一定义，由客户端和服务器共同遵守。

## 设计边界

`Source/NetDriver` 只负责“对象如何同步”，不负责：

- 玩家如何登录 / 选服
- 世界服如何发现其他服务
- 场景服的 AOI 与实体管理策略
- 路由、会话校验等

这些逻辑分别由 `WorldServer`、`SceneServer` 与其他服务模块承担。

## 可演进方向

未来可以在现有基础上扩展：

- 深度接入 `AOIComponent`，按区域裁剪可见对象
- 规范复制包格式并版本化
- 聚合脏数据，支持批量复制和频率控制
- 按距离、重要性等维度调节同步频率  
