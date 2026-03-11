# Scene 模块

## 作用

`SceneServer` 负责维护场景内实体视图，是世界服的下游场景协作节点。

当前它的实现还是基础版本，但已经具备最小接入能力。

## 主要职责

- 向 `RouterServer` 注册自身
- 通过 `RouterServer` 动态发现 `WorldServer`
- 主动连接 `WorldServer`
- 接收玩家进入场景、离开场景、位置同步
- 在本地维护场景实体表

## 当前消息职责

### 玩家进入场景

- 输入：`MT_PlayerSwitchServer`
- 行为：在本地场景中创建实体视图

### 玩家离开场景

- 输入：`MT_PlayerLogout`
- 行为：移除实体

### 位置同步

- 输入：`MT_PlayerDataSync`
- 行为：更新实体位置

## 当前场景模型

`SceneServer` 当前围绕两个对象工作：

- `MScene`
- `SSceneEntity`

它们代表本地场景和场景中的实体镜像。

## 与 Router 的关系

`SceneServer` 不再要求固定写死 `WorldServer` 地址才能工作。

当前流程是：

1. 连接 `RouterServer`
2. 注册自己
3. 查询 `WorldServer` 路由
4. 连接目标 `WorldServer`

## 设计边界

`SceneServer` 当前并不是权威世界服，它不负责：

- 账号登录
- 会话校验
- 世界主状态
- 选服策略

它更偏向场景视图与局部协作节点。
