# UE Skill Graph Docs

这组文档是给 UE 技能编辑器实现侧直接落地用的，按“一个核心类型一份文档”拆开，方便分工。

建议阅读顺序：

1. `Docs/UE_SkillGraph_NodeSystem_Design.md`
2. `Docs/UE_SkillGraph/NCSkillCompiledSpec.md`
3. `Docs/UE_SkillGraph/NCSkillGraphNodeBase.md`
4. `Docs/UE_SkillGraph/NCSkillGraphAsset.md`
5. `Docs/UE_SkillGraph/NCSkillGraphCompiler.md`

跨端对齐基准：

- 服务端共享 schema：`Source/Common/Skill/SkillNodeRegistry.def`
- 服务端 schema 视图：`Source/Common/Skill/SkillNodeRegistry.h`
- 服务端编译结果加载：`Source/Servers/Scene/Combat/UAssetSkillLoader.cpp`

约束：

- UE 侧负责图编辑、校验、编译
- 服务器只负责读取 `CompiledSpec` 并执行 `FSkillStep`
- UE 不向服务器暴露节点类实现
- 服务器不执行 UE 节点对象
