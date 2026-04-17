# 文档索引

`Docs/` 是当前仓库唯一的正式文档目录。这里的文档应该描述 `main` 上真实存在的代码结构，而不是保留历史迁移过程。

## 先读这些

1. [Architecture.md](/root/Mession/Docs/Architecture.md)
   说明整体分层、服务职责、对象模型和核心设计约束。
2. [BuildAndRun.md](/root/Mession/Docs/BuildAndRun.md)
   说明如何配置、编译、启动、停服、查看日志、执行验证。
3. [RuntimeAndRpc.md](/root/Mession/Docs/RuntimeAndRpc.md)
   说明反射、RPC、Promise/Future/Coroutine、异步流程组织方式。
4. [Validation.md](/root/Mession/Docs/Validation.md)
   说明当前自动验证覆盖到什么程度，以及改动后应该怎么验。
5. [RequestValidation.md](/root/Mession/Docs/RequestValidation.md)
   说明 `ServerCall` 请求的字段级 Meta 校验约定，以及什么时候仍然需要自定义 validator。

## 与玩法和状态相关

- [GameplayAndState.md](/root/Mession/Docs/GameplayAndState.md)
- [PersistenceAndReplication.md](/root/Mession/Docs/PersistenceAndReplication.md)
- [MObjectAssetSerialization.md](/root/Mession/Docs/MObjectAssetSerialization.md)
- [PlayerRpcDevelopment.md](/root/Mession/Docs/PlayerRpcDevelopment.md)

## 与工具和脚本相关

- [Tooling.md](/root/Mession/Docs/Tooling.md)
- [ServerControlApi.md](/root/Mession/Docs/ServerControlApi.md)
- [ServerRegistry.md](/root/Mession/Docs/ServerRegistry.md)
- [ServerManagerTui.md](/root/Mession/Docs/ServerManagerTui.md)
- [K8sContainerPrep.md](/root/Mession/Docs/K8sContainerPrep.md)

## 与 UE 和技能图相关

- [UE_ClientSkeletonDesign.md](/root/Mession/Docs/UE_ClientSkeletonDesign.md)
- [UE_LoginIntegrationGuide.md](/root/Mession/Docs/UE_LoginIntegrationGuide.md)
- [UE_HeartbeatIntegrationGuide.md](/root/Mession/Docs/UE_HeartbeatIntegrationGuide.md)
- [UE_PlayerSyncAgentGuide.md](/root/Mession/Docs/UE_PlayerSyncAgentGuide.md)
- [UE_SkillGraph_NodeSystem_Design.md](/root/Mession/Docs/UE_SkillGraph_NodeSystem_Design.md)
- [UE_SkillGraph/README.md](/root/Mession/Docs/UE_SkillGraph/README.md)

## 与后续演进相关

- [Roadmap.md](/root/Mession/Docs/Roadmap.md)

## 当前推荐的阅读路径

如果你的目标是理解并继续开发当前仓库，建议这样读：

1. 先看架构和构建
2. 再看玩法状态和 Player RPC
3. 然后看验证与工具链
4. 最后再进入 UE 集成、控制面、多机和技能图专题

## 文档维护约定

- 文档描述的是当前代码事实，不保留“旧版方案还在这里供参考”的写法
- 如果文档与代码冲突，以代码为准，并应直接更新文档
- 新增模块时，优先补 `Architecture.md` 和对应专题文档
- 如果一个 TODO 已经落地，应同步更新 `README`、`Validation`、`Roadmap` 或专题文档，而不是只改代码
