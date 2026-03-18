# Docs

`Docs/` 是 Mession 唯一的正式文档目录。  
这里的文档分三类：

- 架构与阶段决策
- 协议与开发流程
- 源码模块说明

## 推荐阅读路径

### 路线 1：理解当前主链路

1. [client-unified-function-call.md](/workspaces/Mession/Docs/client-unified-function-call.md)
2. [function-id-rules.md](/workspaces/Mession/Docs/function-id-rules.md)
3. [validation.md](/workspaces/Mession/Docs/validation.md)
4. [TODO.md](/workspaces/Mession/Docs/TODO.md)

### 路线 2：理解 Gameplay 方向

1. [gameplay-avatar-framework.md](/workspaces/Mession/Docs/gameplay-avatar-framework.md)
2. [ue-gateway-quickstart.md](/workspaces/Mession/Docs/ue-gateway-quickstart.md)
3. [ue-client-downlink-function-call.md](/workspaces/Mession/Docs/ue-client-downlink-function-call.md)

### 路线 3：理解底层设施

1. [mheadertool-design.md](/workspaces/Mession/Docs/mheadertool-design.md)
2. [event-loop-architecture.md](/workspaces/Mession/Docs/event-loop-architecture.md)
3. [socket-layer-refactor.md](/workspaces/Mession/Docs/socket-layer-refactor.md)
4. [protocol-byteorder.md](/workspaces/Mession/Docs/protocol-byteorder.md)

## 文档索引

### 当前主线

- [TODO.md](/workspaces/Mession/Docs/TODO.md)
- [next-steps.md](/workspaces/Mession/Docs/next-steps.md)
- [validation.md](/workspaces/Mession/Docs/validation.md)

### Client / UE / Protocol

- [client-unified-function-call.md](/workspaces/Mession/Docs/client-unified-function-call.md)
- [function-id-rules.md](/workspaces/Mession/Docs/function-id-rules.md)
- [client-protocol-reflection.md](/workspaces/Mession/Docs/client-protocol-reflection.md)
- [ue-gateway-quickstart.md](/workspaces/Mession/Docs/ue-gateway-quickstart.md)
- [ue-client-downlink-function-call.md](/workspaces/Mession/Docs/ue-client-downlink-function-call.md)
- [protocol-byteorder.md](/workspaces/Mession/Docs/protocol-byteorder.md)
- [module-protocol.md](/workspaces/Mession/Docs/module-protocol.md)

### Gameplay

- [gameplay-avatar-framework.md](/workspaces/Mession/Docs/gameplay-avatar-framework.md)

### Infrastructure / Tooling

- [mheadertool-design.md](/workspaces/Mession/Docs/mheadertool-design.md)
- [event-loop-architecture.md](/workspaces/Mession/Docs/event-loop-architecture.md)
- [eventloop-asio-survey.md](/workspaces/Mession/Docs/eventloop-asio-survey.md)
- [socket-layer-refactor.md](/workspaces/Mession/Docs/socket-layer-refactor.md)
- [server-template.md](/workspaces/Mession/Docs/server-template.md)
- [logging-design.md](/workspaces/Mession/Docs/logging-design.md)
- [base-library.md](/workspaces/Mession/Docs/base-library.md)
- [taskqueue-threadpool.md](/workspaces/Mession/Docs/taskqueue-threadpool.md)
- [async-promise-coroutine.md](/workspaces/Mession/Docs/async-promise-coroutine.md)

### Codebase Modules

- [module-core.md](/workspaces/Mession/Docs/module-core.md)
- [module-common.md](/workspaces/Mession/Docs/module-common.md)
- [module-netdriver.md](/workspaces/Mession/Docs/module-netdriver.md)
- [module-gateway.md](/workspaces/Mession/Docs/module-gateway.md)
- [module-login.md](/workspaces/Mession/Docs/module-login.md)
- [module-world.md](/workspaces/Mession/Docs/module-world.md)
- [module-scene.md](/workspaces/Mession/Docs/module-scene.md)
- [module-router.md](/workspaces/Mession/Docs/module-router.md)

## 维护原则

- 根 `README.md`
  - 只放总览和快速开始

- `Docs/`
  - 放正式设计、约束和模块说明

如果两份文档冲突，以 `Docs/` 中更具体的文档为准。
