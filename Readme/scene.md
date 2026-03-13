# Scene 模块（`Source/Servers/Scene`）

`SceneServer` 负责维护场景内实体视图，是 `WorldServer` 的下游场景协作节点。  
当前实现仍然偏基础，但已经具备最小接入与同步能力。

## 主要职责

- 向 `RouterServer` 注册自身
- 通过 `RouterServer` 动态发现 `WorldServer`
- 主动连接 `WorldServer`
- 接收玩家进入场景、离开场景、位置同步
- 在本地维护场景实体表与镜像状态

## 消息职责

### 玩家进入场景

- 输入：`MT_PlayerSwitchServer`
- 行为：在本地场景中创建实体视图（通常是 `SSceneEntity`）

### 玩家离开场景

- 输入：`MT_PlayerLogout`
- 行为：从场景中移除对应实体

### 位置/状态同步

- 输入：`MT_PlayerDataSync`
- 行为：更新场景实体位置/状态

这里的 `MT_PlayerDataSync` 专门表示 `World -> Scene` 的状态同步，不再承担 `Gateway -> World` 的客户端包转发职责。

## 场景模型

`SceneServer` 当前围绕两个核心对象工作：

- `MScene`：场景本身
- `SSceneEntity`：场景中的实体镜像

`WorldServer` 仍是权威世界服，`SceneServer` 只维护**视图数据**，用于附近实体展示、调试或后续的 AOI 拓展。

## 与 Router 的关系

`SceneServer` 不再依赖硬编码的 `WorldServer` 地址，启动流程大致为：

1. 连接 `RouterServer`
2. 注册自身（`MT_ServerRegister`）
3. 查询目标 `WorldServer` 路由（可带 `ZoneId` 等条件）
4. 主动连接目标 `WorldServer`

## 设计边界

`SceneServer` 不是权威世界服，它不负责：

- 账号登录
- 会话校验
- 世界主状态
- 玩家选服与路由策略

它更偏向：**场景视图与局部协作节点**，未来可以在这里扩展 AOI、可见性裁剪等能力。  
