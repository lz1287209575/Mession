# NCSkillCompiledSpec

## 目标

`FNCSkillCompiledSpec` 和 `FNCSkillCompiledStep` 是 UE 技能图编译后的运行时交换格式。

它们的职责只有两个：

- 保存到 `UNCSkillGraphAsset` 的 `CompiledSpec`
- 被服务器从 `.uasset` 中读取并映射到 `FSkillSpec`

它们不承担编辑器图信息，不保存布局、注释、节点坐标等 EditorOnly 数据。

## 推荐结构

```cpp
USTRUCT(BlueprintType)
struct FNCSkillCompiledStep
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    int32 StepIndex = 0;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    uint16 NodeId = 0;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    FName NodeType;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    TArray<int32> NextStepIndices;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    float FloatParam0 = 0.f;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    float FloatParam1 = 0.f;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    int32 IntParam0 = 0;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    FName NameParam;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    FString StringParam;
};

USTRUCT(BlueprintType)
struct FNCSkillCompiledSpec
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    FName SkillId;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    float CooldownSeconds = 0.f;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    float CastTimeSeconds = 0.f;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    float MaxRange = 0.f;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    TEnumAsByte<ENCSkillTargetType> TargetType;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    TArray<FNCSkillCompiledStep> Steps;
};
```

## 字段约定

### `NodeId`

- 稳定数值 ID
- 应与 `Source/Common/Skill/SkillNodeRegistry.def` 中定义一致
- 服务器未来优先按这个字段识别节点

### `NodeType`

- 建议写入 `RegistryName`
- 主要用于调试、日志和兼容旧资源
- 即使有 `NodeId`，也建议保留

### `NextStepIndices`

- 编译器计算出的后继 step 序号
- 表达运行时顺序和分支
- 不保存编辑器原始边对象

### `FloatParam0 / FloatParam1 / IntParam0 / NameParam / StringParam`

- 通用槽位
- 语义由共享 schema 决定
- UE 侧不要把它们当业务字段名直接暴露给策划

## 推荐编译规则

- `StepIndex` 从 0 开始连续编号
- `NodeId` 必须非 0
- `NodeType` 必须非空
- `NextStepIndices` 只允许指向有效 step
- 通用参数槽未使用时保留默认值

## 与服务器的对应关系

服务器当前映射链路：

```text
FNCSkillCompiledSpec
  -> FCompiledSkillDefinition
  -> FSkillSpec
  -> FSkillStep
```

当前服务端兼容策略：

1. 优先按 `RegistryName`
2. 兼容按 `EditorNodeToken` 模糊匹配
3. 后续建议升级为优先按 `NodeId`

## 不建议放入 CompiledSpec 的内容

- 节点位置
- 注释文本
- 编辑器折叠状态
- pin 可视化数据
- Slate/GraphEditor 相关对象
- 任何仅编辑器可用的 UObject 引用
