# NCSkillGraphCompiler

## 目标

`FNCSkillGraphCompiler` 负责把 `UNCSkillGraphAsset` 的编辑器图数据编译为 `FNCSkillCompiledSpec`。

它是 UE 侧最关键的运行前转换器。

## 职责

- 找到合法起点
- 遍历整张图
- 给节点分配 `StepIndex`
- 调用节点级校验
- 做图级校验
- 生成 `CompiledSpec`

## 推荐接口

```cpp
struct FNCSkillCompileContext
{
    const UNCSkillGraphAsset* Asset = nullptr;
    TMap<const UNCSkillGraphNodeBase*, int32> NodeToStepIndex;
    TArray<FText> Errors;
    TArray<FText> Warnings;

    int32 AllocateStepIndex(const UNCSkillGraphNodeBase* Node);
    TArray<int32> ResolveOutgoingStepIndices(const UNCSkillGraphNodeBase* Node) const;
    void AddError(const UObject* SourceObject, const FString& Message);
    void AddWarning(const UObject* SourceObject, const FString& Message);
};

class FNCSkillGraphCompiler
{
public:
    bool Compile(UNCSkillGraphAsset* Asset, FText& OutError);

private:
    bool ValidateGraph(UNCSkillGraphAsset* Asset, FNCSkillCompileContext& Context);
    bool BuildCompiledSpec(UNCSkillGraphAsset* Asset, FNCSkillCompileContext& Context);
    UNCSkillGraphNodeBase* FindStartNode(const UNCSkillGraphAsset* Asset) const;
    void CollectReachableNodes(
        const UNCSkillGraphNodeBase* StartNode,
        TArray<const UNCSkillGraphNodeBase*>& OutNodes) const;
};
```

## 编译流程建议

### 第一步：基础校验

- `Asset` 非空
- `Nodes` 非空
- 存在且仅存在一个 `Start`

### 第二步：可达节点收集

- 从 `Start` 开始遍历
- 收集所有可达节点
- 构建稳定顺序

建议：

- 遍历顺序稳定
- 同一张图多次编译输出一致

### 第三步：分配 `StepIndex`

- 按稳定顺序为节点分配 step 编号
- `StepIndex` 从 0 开始连续增长

### 第四步：节点级校验

- 逐个调用 `ValidateNode`
- 收集错误和警告

### 第五步：图级校验

- 至少存在一个 `End`
- 所有资源内节点都从 `Start` 可达
- 不允许空出边目标
- 不允许未知 schema 节点

### 第六步：构建 `CompiledSpec`

- 写入基础技能配置
- 逐个节点调用 `BuildCompiledStep`
- 生成完整 step 数组

## 推荐伪代码

```cpp
bool FNCSkillGraphCompiler::Compile(UNCSkillGraphAsset* Asset, FText& OutError)
{
    if (!Asset)
    {
        OutError = FText::FromString(TEXT("Asset is null."));
        return false;
    }

    FNCSkillCompileContext Context;
    Context.Asset = Asset;

    if (!ValidateGraph(Asset, Context))
    {
        OutError = Context.Errors.Num() > 0
            ? Context.Errors[0]
            : FText::FromString(TEXT("Graph validation failed."));
        return false;
    }

    if (!BuildCompiledSpec(Asset, Context))
    {
        OutError = FText::FromString(TEXT("BuildCompiledSpec failed."));
        return false;
    }

    Asset->bHasCompiledSpec = true;
    return true;
}
```

## 输出填充建议

`BuildCompiledSpec` 建议至少填充：

```cpp
Asset->CompiledSpec.SkillId = Asset->SkillId;
Asset->CompiledSpec.CooldownSeconds = Asset->CooldownSeconds;
Asset->CompiledSpec.CastTimeSeconds = Asset->CastTimeSeconds;
Asset->CompiledSpec.MaxRange = Asset->MaxRange;
Asset->CompiledSpec.TargetType = Asset->TargetType;
Asset->CompiledSpec.Steps = CompiledSteps;
```

## 错误策略

建议将编译错误分为：

- `Error`：阻止写入 `CompiledSpec`
- `Warning`：允许写入，但提示策划

典型 `Error`：

- 没有 `Start`
- 多个 `Start`
- `RequiredRange <= 0`
- 出边数量不合法
- 编译后 step 索引非法

典型 `Warning`：

- `AttackPowerScale == 0`
- 有未使用参数槽
- 图存在可优化的冗余节点

## 稳定性要求

编译器必须保证：

- 同一资产多次编译结果一致
- `StepIndex` 稳定
- `NodeId` 与 schema 一致
- `NodeType` 使用稳定 `RegistryName`

这对服务器解析和差异比对很重要。

## 与服务器的边界

编译器不要做以下事情：

- 不做战斗数值结算
- 不模拟服务端执行结果
- 不依赖服务器内存对象
- 不写服务器专用流程逻辑

编译器只负责把图“翻译”为受限运行时格式。
