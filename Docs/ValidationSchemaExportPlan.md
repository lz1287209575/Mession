# Validation Schema Export Plan

## 目标

把 validate 当前使用的迁移期兼容 Schema，替换为由 `MHeaderTool` 在生成阶段直接导出的正式协议 Schema 文件。

目标产物：

```text
Build/Generated/ValidationProtocolSchema.json
```

这样 Python validate 工具就不需要长期维护仓库内置的 `compat_protocol_schema.json`，而是直接消费代码生成产物。

## 当前基础

仓库当前已经具备两块关键基础：

- C++ 侧有 `MHeaderTool`，能扫描 `MSTRUCT` / `MCLASS` / `MPROPERTY`
- Python validate 侧已经有：
  - `Scripts/validation/schema_loader.py`
  - `Scripts/validation/transport/reflection_decoder.py`

也就是说，validate 消费端骨架已经准备好，缺的是正式导出端。

## 推荐接入点

`MHeaderTool` 当前已经有多种生成产物：

- `WriteGeneratedFiles(...)`
- `WriteGeneratedRpcManifest(...)`
- `WriteGeneratedReflectionManifest(...)`
- `WriteGeneratedClientManifest(...)`
- `WriteCMakeManifest(...)`

建议新增一条并列生成路径：

- `WriteValidationProtocolSchema(...)`

推荐接入位置：

- 文件：`Source/Tools/MHeaderTool.cpp`
- 时机：在所有 `Classes` 已解析完成后，与其他 manifest 一样统一生成

也就是在 `main()` 的生成流程里新增：

1. 解析参数，拿到 `--validation-schema-out=...`
2. 在 `Classes` 已构建完成后调用 `WriteValidationProtocolSchema(...)`

## 推荐 CLI 参数

建议在 `MHeaderTool` 中新增参数：

```text
--validation-schema-out=Build/Generated/ValidationProtocolSchema.json
```

如果未显式指定，可默认写到：

```text
Build/Generated/ValidationProtocolSchema.json
```

## 推荐导出范围

第一阶段不建议一口气导出所有反射类型，而是优先导出 validate 真实会消费的协议相关结构。

建议范围：

- `Source/Protocol/Messages/**`
- 以及这些目录下由 `MSTRUCT()` 标记的结构

优先级排序：

1. Gateway Client response / notify
2. Scene sync message
3. Combat client response
4. 其余 World / Router / Login / Mgo 协议结构

这样可以把范围控制在“线协议结构”，避免把大量内部对象状态结构一起导出。

## 导出过滤规则

建议第一阶段使用以下过滤条件：

- 只导出 `Parsed.Kind == Struct`
- 只导出源文件路径位于 `Source/Protocol/Messages/`
- 只导出有 `MPROPERTY()` 的字段

后续如果需要，可以再扩展：

- 允许按类型元数据显式标记“仅供验证导出”
- 或允许工具按目录 / Owner / Namespace 再做细筛

## 字段导出要求

导出的不是“C++ 类型声明文本”，而是 validate 可直接消费的线协议字段描述。

每个字段至少需要导出：

- `name`
- `kind`
- 对于嵌套结构的 `type_name`
- 对于 vector 的 `item_kind`
- 对于 struct vector 的 `item_type_name`
- 对于 padding / bytes 的 `size`

## 类型映射建议

第一阶段建议支持以下映射：

- `bool` -> `bool`
- `uint8` / `E... : uint8` -> `u8`
- `uint16` -> `u16`
- `uint32` -> `u32`
- `uint64` -> `u64`
- `float` -> `f32`
- `MString` -> `string`
- `TVector<uint64>` -> `vector(item_kind=u64)`
- `TVector<Struct>` -> `vector(item_kind=struct, item_type_name=...)`
- 反射 struct -> `struct(type_name=...)`

## 关于 padding

当前 validate 已验证过一个现实问题：

- `FCombatUnitRef` 当前在线协议里表现为 `uint8 + 7 bytes padding + uint64 + uint64`

这说明第一版导出器不能只看“字段声明顺序”，还需要能表达线协议中的显式 padding。

建议策略：

### 第一阶段

允许在 `WriteValidationProtocolSchema(...)` 中为已知特殊结构补显式规则：

- `FCombatUnitRef`

即：

```json
[
  { "name": "UnitKind", "kind": "u8" },
  { "name": "_pad0", "kind": "padding", "size": 7 },
  { "name": "CombatEntityId", "kind": "u64" },
  { "name": "PlayerId", "kind": "u64" }
]
```

### 第二阶段

再考虑把“线协议布局规则”正式收口到序列化层或导出层，减少这种特殊分支。

## 实现步骤建议

### Step 1：新增导出选项和输出函数

在 `MHeaderTool.cpp` 中新增：

- `Options.ValidationSchemaPath`
- `WriteValidationProtocolSchema(...)`

先跑通最小 JSON 输出。

### Step 2：导出最小协议集

先覆盖 validate 当前已经消费到的结构：

- `FClientLoginResponse`
- `FClientFindPlayerResponse`
- `FClientSwitchSceneResponse`
- `FClientQueryProfileResponse`
- `FClientQueryInventoryResponse`
- `FClientQueryProgressionResponse`
- `FClientQueryCombatProfileResponse`
- `FClientSetPrimarySkillResponse`
- `FClientDebugSpawnMonsterResponse`
- `FClientCastSkillAtUnitResponse`
- `SPlayerSceneStateMessage`
- `SPlayerSceneLeaveMessage`
- `FCombatUnitRef`

### Step 3：validate 默认优先消费生成文件

这一步代码已经基本具备：

- `schema_loader.load_schema_with_fallback(...)`

它会：

1. 先尝试加载 `Build/Generated/ValidationProtocolSchema.json`
2. 如果没有，再回退到 `compat_protocol_schema.json`

### Step 4：逐步淘汰兼容 Schema

当正式导出稳定后：

- 逐步减少 `compat_protocol_schema.json`
- 最终只保留极小规模的回退能力，或者完全移除

## 风险点

### 1. 反射类型信息不足以直接表达线协议

尤其是：

- padding
- 特殊容器
- 未来可能出现的原始字节数组

所以第一版导出器可以允许存在少量“已知特殊类型补丁”。

### 2. validate 使用的是线协议，而不是普通反射对象语义

因此导出器必须明确：

- 面向 validate 的 Schema 是“线协议布局描述”
- 不能简单等同于“反射字段列表”

### 3. 生成时机

需要保证：

- 初次 configure / build 时 schema 文件就能生成
- 与 `MReflectionManifest.generated.h` 等生成产物保持同步

## 当前建议

推荐优先级如下：

1. 在 `MHeaderTool` 增加 `ValidationProtocolSchema.json` 导出能力
2. 先覆盖 validate 当前已经接入 schema decoder 的类型
3. 再逐步扩展到所有 Gateway / Scene / Combat 相关协议

这样可以让 validate 工具尽快摆脱长期维护兼容 schema 的状态。
