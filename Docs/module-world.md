# Module: World

`Source/Servers/World` 是当前主要的运行时业务服。

## 当前职责

- 接收 Gateway 转发的已鉴权客户端请求
- 创建与管理玩家 Avatar
- 驱动 Gameplay tick
- 驱动 replication
- 协同 Scene

## 当前原则

World 负责流程编排与权威状态维护，  
但不应该继续长成“所有业务都写在一个 cpp 里”的巨石。

Gameplay 规则应逐步下沉到 `Source/Gameplay/`。
