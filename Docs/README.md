# 文档索引

`Docs/` 是当前仓库唯一的正式文档目录。这里不再保留“旧设计说明 + 新实现说明”并存的结构，所有文档都以当前代码事实为准。

## 先读这些

1. [Architecture.md](/root/Mession/Docs/Architecture.md)
   说明整体分层、服务职责、目录结构与核心设计约束。
2. [BuildAndRun.md](/root/Mession/Docs/BuildAndRun.md)
   说明如何配置、编译、启动、停服、查看日志。
3. [RuntimeAndRpc.md](/root/Mession/Docs/RuntimeAndRpc.md)
   说明反射、RPC、Promise/Future/Coroutine、异步流程组织方式。

## 对玩法与状态相关的文档

- [GameplayAndState.md](/root/Mession/Docs/GameplayAndState.md)
- [PersistenceAndReplication.md](/root/Mession/Docs/PersistenceAndReplication.md)
- [PlayerRpcDevelopment.md](/root/Mession/Docs/PlayerRpcDevelopment.md)

## 对工具与验证相关的文档

- [Tooling.md](/root/Mession/Docs/Tooling.md)
- [Validation.md](/root/Mession/Docs/Validation.md)
- [UE_LoginIntegrationGuide.md](/root/Mession/Docs/UE_LoginIntegrationGuide.md)
- [UE_HeartbeatIntegrationGuide.md](/root/Mession/Docs/UE_HeartbeatIntegrationGuide.md)
- [UE_PlayerSyncAgentGuide.md](/root/Mession/Docs/UE_PlayerSyncAgentGuide.md)
- [UE_LoginAgentPrompt.md](/root/Mession/Docs/UE_LoginAgentPrompt.md)
- [UE_ClientSkeletonDesign.md](/root/Mession/Docs/UE_ClientSkeletonDesign.md)
- [UE_ServerProjectManagerSpec.md](/root/Mession/Docs/UE_ServerProjectManagerSpec.md)

## 对后续演进相关的文档

- [Roadmap.md](/root/Mession/Docs/Roadmap.md)

## 文档约定

- 文档描述的是当前 `main` 上的真实代码结构，不再保存历史迁移步骤。
- 如果文档与代码冲突，以代码为准，并应直接更新这里的文档。
- 若新增模块，请优先补 `Architecture.md` 和对应专题文档，而不是再创建零散备忘。
