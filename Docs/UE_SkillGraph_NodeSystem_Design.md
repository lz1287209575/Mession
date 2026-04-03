# UE 技能图节点系统落地设计

## 1. 目标

本设计用于在 UE 编辑器内实现技能图编排，并将图编译为服务器可直接读取的 `CompiledSpec`。

要求：

- 技能源资源仍然是 `.uasset`
- UE 内可视化编排节点
- 保存编辑器图数据
- 同时保存编译结果 `CompiledSpec`
- 服务器只读取 `CompiledSpec`
- UE 节点定义与服务器共享同一份节点 schema

---

## 2. UE 侧总体结构

```text
UNCSkillGraphAsset
  ├── 技能基础配置
  ├── 编辑器节点列表
  ├── 编辑器连线关系
  └── CompiledSpec

UNCSkillGraphNodeBase
  ├── 节点标识
  ├── 通用参数槽
  ├── 连线
  ├── 校验
  └── 编译输出

UNCSkillGraphCompiler
  ├── 图遍历
  ├── 节点校验
  ├── step 编号
  └── CompiledSpec 生成

共享节点 schema
  └── SkillNodeRegistry.def / SkillNodeRegistry.h
```

---

## 3. 资源定义

## 3.1 技能图资源

建议 UE 资源定义如下：

```cpp
UCLASS(BlueprintType)
class UNCSkillGraphAsset : public UObject
{
    GENERATED_BODY()

public:
    // 基础配置
    UPROPERTY(EditAnywhere, Category="Skill")
    FName SkillId;

    UPROPERTY(EditAnywhere, Category="Skill")
    float CooldownSeconds = 0.f;

    UPROPERTY(EditAnywhere, Category="Skill")
    float CastTimeSeconds = 0.f;

    UPROPERTY(EditAnywhere, Category="Skill")
    float MaxRange = 0.f;

    UPROPERTY(EditAnywhere, Category="Skill")
    TEnumAsByte<ENCSkillTargetType> TargetType;

    // 编辑器图数据
    UPROPERTY(Instanced)
    TArray<TObjectPtr<class UNCSkillGraphNodeBase>> Nodes;

    // 编译结果
    UPROPERTY(VisibleAnywhere, Category="Compiled")
    bool bHasCompiledSpec = false;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    int32 CompiledSchemaVersion = 1;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    FNCSkillCompiledSpec CompiledSpec;
};
```

---

## 4. 编译产物定义

## 4.1 单个 Step

```cpp
USTRUCT(BlueprintType)
struct FNCSkillCompiledStep
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    int32 StepIndex = 0;

    // 稳定节点 ID，供服务器优先匹配
    UPROPERTY(VisibleAnywhere, Category="Compiled")
    uint16 NodeId = 0;

    // 稳定注册名，供日志/兼容/回退匹配
    UPROPERTY(VisibleAnywhere, Category="Compiled")
    FName NodeType;

    // 下一步 step 索引
    UPROPERTY(VisibleAnywhere, Category="Compiled")
    TArray<int32> NextStepIndices;

    // 通用参数槽
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
```

## 4.2 整体 CompiledSpec

```cpp
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

---

## 5. 节点基类设计

## 5.1 设计目标

`UNCSkillGraphNodeBase` 只负责：

- 编辑器显示
- 参数编辑
- 连线管理
- 编译前校验
- 编译成 step

它不负责：

- 真实战斗执行
- 伤害计算
- 服务端上下文访问

---

## 5.2 基类定义

```cpp
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class UNCSkillGraphNodeBase : public UObject
{
    GENERATED_BODY()

public:
    // ===== 节点身份 =====

    // 必须和共享 schema 对齐
    virtual uint16 GetStableNodeId() const PURE_VIRTUAL(UNCSkillGraphNodeBase::GetStableNodeId, return 0;);
    virtual FName GetRegistryName() const PURE_VIRTUAL(UNCSkillGraphNodeBase::GetRegistryName, return NAME_None;);

    // ===== 编辑器显示 =====

    virtual FText GetNodeDisplayName() const;
    virtual FName GetNodeCategory() const;

    // ===== 连线约束 =====

    virtual int32 GetMinOutgoingEdges() const;
    virtual int32 GetMaxOutgoingEdges() const;

    // ===== 校验 =====

    virtual void ValidateNode(struct FNCSkillCompileContext& Context) const;

    // ===== 编译 =====

    virtual bool BuildCompiledStep(
        struct FNCSkillCompileContext& Context,
        struct FNCSkillCompiledStep& OutStep) const PURE_VIRTUAL(UNCSkillGraphNodeBase::BuildCompiledStep, return false;);

public:
    // 编辑器唯一 NodeGuid，便于图编辑器追踪
    UPROPERTY()
    FGuid NodeGuid;

    // 通用参数槽
    UPROPERTY(EditAnywhere, Category="Skill Params")
    float FloatParam0 = 0.f;

    UPROPERTY(EditAnywhere, Category="Skill Params")
    float FloatParam1 = 0.f;

    UPROPERTY(EditAnywhere, Category="Skill Params")
    int32 IntParam0 = 0;

    UPROPERTY(EditAnywhere, Category="Skill Params")
    FName NameParam;

    UPROPERTY(EditAnywhere, Category="Skill Params")
    FString StringParam;

    // 出边
    UPROPERTY()
    TArray<TObjectPtr<UNCSkillGraphNodeBase>> OutgoingNodes;
};
```

---

## 6. 节点基类默认行为

## 6.1 显示信息从 schema 获取

推荐不要在每个节点子类里硬编码：

- 显示名
- 分类
- 最大出口数
- 参数显示名

而是从共享 schema 查询。

建议基类内部加一个辅助函数：

```cpp
const FSkillNodeRegistryEntry* GetRegistryEntry() const;
```

然后默认实现：

```cpp
FText UNCSkillGraphNodeBase::GetNodeDisplayName() const
{
    if (const FSkillNodeRegistryEntry* Entry = GetRegistryEntry())
    {
        return FText::FromString(FString(Entry->DisplayName.GetData()));
    }
    return FText::FromName(GetRegistryName());
}

FName UNCSkillGraphNodeBase::GetNodeCategory() const
{
    if (const FSkillNodeRegistryEntry* Entry = GetRegistryEntry())
    {
        return ConvertCategoryToName(Entry->Category);
    }
    return TEXT("Unknown");
}

int32 UNCSkillGraphNodeBase::GetMinOutgoingEdges() const
{
    if (const FSkillNodeRegistryEntry* Entry = GetRegistryEntry())
    {
        return Entry->MinOutgoingEdges;
    }
    return 0;
}

int32 UNCSkillGraphNodeBase::GetMaxOutgoingEdges() const
{
    if (const FSkillNodeRegistryEntry* Entry = GetRegistryEntry())
    {
        return Entry->MaxOutgoingEdges;
    }
    return 0;
}
```

---

## 6.2 默认校验逻辑

基类默认校验应包含：

- `StableNodeId` 必须可在 schema 中找到
- `RegistryName` 必须可在 schema 中找到
- 出口数必须满足 schema 约束
- 所有出边目标不能为空

建议：

```cpp
void UNCSkillGraphNodeBase::ValidateNode(FNCSkillCompileContext& Context) const
{
    const FSkillNodeRegistryEntry* Entry = GetRegistryEntry();
    if (!Entry)
    {
        Context.AddError(this, TEXT("Node schema entry not found."));
        return;
    }

    const int32 OutEdgeCount = OutgoingNodes.Num();
    if (OutEdgeCount < Entry->MinOutgoingEdges || OutEdgeCount > Entry->MaxOutgoingEdges)
    {
        Context.AddError(
            this,
            FString::Printf(TEXT("Outgoing edge count invalid. expected [%d, %d], actual %d"),
                Entry->MinOutgoingEdges,
                Entry->MaxOutgoingEdges,
                OutEdgeCount));
    }

    for (const TObjectPtr<UNCSkillGraphNodeBase>& NextNode : OutgoingNodes)
    {
        if (!NextNode)
        {
            Context.AddError(this, TEXT("Outgoing node is null."));
        }
    }
}
```

---

## 7. 子类设计

## 7.1 最小节点集合

建议先实现：

- `UNCStartSkillNode`
- `UNCSelectTargetSkillNode`
- `UNCCheckRangeSkillNode`
- `UNCApplyDamageSkillNode`
- `UNCEndSkillNode`

---

## 7.2 Start 节点

```cpp
UCLASS()
class UNCStartSkillNode : public UNCSkillGraphNodeBase
{
    GENERATED_BODY()

public:
    virtual uint16 GetStableNodeId() const override { return 1; }
    virtual FName GetRegistryName() const override { return TEXT("Start"); }

    virtual bool BuildCompiledStep(
        FNCSkillCompileContext& Context,
        FNCSkillCompiledStep& OutStep) const override
    {
        OutStep.StepIndex = Context.AllocateStepIndex(this);
        OutStep.NodeId = GetStableNodeId();
        OutStep.NodeType = GetRegistryName();
        OutStep.NextStepIndices = Context.ResolveOutgoingStepIndices(this);
        return true;
    }
};
```

---

## 7.3 SelectTarget 节点

```cpp
UCLASS()
class UNCSelectTargetSkillNode : public UNCSkillGraphNodeBase
{
    GENERATED_BODY()

public:
    virtual uint16 GetStableNodeId() const override { return 2; }
    virtual FName GetRegistryName() const override { return TEXT("SelectTarget"); }

    virtual bool BuildCompiledStep(
        FNCSkillCompileContext& Context,
        FNCSkillCompiledStep& OutStep) const override
    {
        OutStep.StepIndex = Context.AllocateStepIndex(this);
        OutStep.NodeId = GetStableNodeId();
        OutStep.NodeType = GetRegistryName();
        OutStep.NextStepIndices = Context.ResolveOutgoingStepIndices(this);
        return true;
    }
};
```

---

## 7.4 CheckRange 节点

```cpp
UCLASS()
class UNCCheckRangeSkillNode : public UNCSkillGraphNodeBase
{
    GENERATED_BODY()

public:
    virtual uint16 GetStableNodeId() const override { return 3; }
    virtual FName GetRegistryName() const override { return TEXT("CheckRange"); }

    virtual void ValidateNode(FNCSkillCompileContext& Context) const override
    {
        Super::ValidateNode(Context);

        if (FloatParam0 <= 0.f)
        {
            Context.AddError(this, TEXT("RequiredRange must be > 0."));
        }
    }

    virtual bool BuildCompiledStep(
        FNCSkillCompileContext& Context,
        FNCSkillCompiledStep& OutStep) const override
    {
        OutStep.StepIndex = Context.AllocateStepIndex(this);
        OutStep.NodeId = GetStableNodeId();
        OutStep.NodeType = GetRegistryName();
        OutStep.NextStepIndices = Context.ResolveOutgoingStepIndices(this);

        // FloatParam0 == RequiredRange
        OutStep.FloatParam0 = FloatParam0;
        return true;
    }
};
```

---

## 7.5 ApplyDamage 节点

```cpp
UCLASS()
class UNCApplyDamageSkillNode : public UNCSkillGraphNodeBase
{
    GENERATED_BODY()

public:
    virtual uint16 GetStableNodeId() const override { return 4; }
    virtual FName GetRegistryName() const override { return TEXT("ApplyDamage"); }

    virtual void ValidateNode(FNCSkillCompileContext& Context) const override
    {
        Super::ValidateNode(Context);

        if (FloatParam0 < 0.f)
        {
            Context.AddError(this, TEXT("BaseDamage must be >= 0."));
        }

        if (FloatParam1 < 0.f)
        {
            Context.AddWarning(this, TEXT("AttackPowerScale is negative."));
        }
    }

    virtual bool BuildCompiledStep(
        FNCSkillCompileContext& Context,
        FNCSkillCompiledStep& OutStep) const override
    {
        OutStep.StepIndex = Context.AllocateStepIndex(this);
        OutStep.NodeId = GetStableNodeId();
        OutStep.NodeType = GetRegistryName();
        OutStep.NextStepIndices = Context.ResolveOutgoingStepIndices(this);

        // FloatParam0 == BaseDamage
        // FloatParam1 == AttackPowerScale
        OutStep.FloatParam0 = FloatParam0;
        OutStep.FloatParam1 = FloatParam1;
        return true;
    }
};
```

---

## 7.6 End 节点

```cpp
UCLASS()
class UNCEndSkillNode : public UNCSkillGraphNodeBase
{
    GENERATED_BODY()

public:
    virtual uint16 GetStableNodeId() const override { return 5; }
    virtual FName GetRegistryName() const override { return TEXT("End"); }

    virtual bool BuildCompiledStep(
        FNCSkillCompileContext& Context,
        FNCSkillCompiledStep& OutStep) const override
    {
        OutStep.StepIndex = Context.AllocateStepIndex(this);
        OutStep.NodeId = GetStableNodeId();
        OutStep.NodeType = GetRegistryName();
        return true;
    }
};
```

---

## 8. 编译上下文设计

```cpp
struct FNCSkillCompileContext
{
    const UNCSkillGraphAsset* Asset = nullptr;

    TMap<const UNCSkillGraphNodeBase*, int32> NodeToStepIndex;

    TArray<FText> Errors;
    TArray<FText> Warnings;

    int32 AllocateStepIndex(const UNCSkillGraphNodeBase* Node)
    {
        if (const int32* Found = NodeToStepIndex.Find(Node))
        {
            return *Found;
        }

        const int32 NewIndex = NodeToStepIndex.Num();
        NodeToStepIndex.Add(Node, NewIndex);
        return NewIndex;
    }

    TArray<int32> ResolveOutgoingStepIndices(const UNCSkillGraphNodeBase* Node) const
    {
        TArray<int32> Result;
        if (!Node)
        {
            return Result;
        }

        for (const TObjectPtr<UNCSkillGraphNodeBase>& NextNode : Node->OutgoingNodes)
        {
            if (!NextNode)
            {
                continue;
            }

            if (const int32* Found = NodeToStepIndex.Find(NextNode))
            {
                Result.Add(*Found);
            }
        }
        return Result;
    }

    void AddError(const UObject* SourceObject, const FString& Message)
    {
        Errors.Add(FText::FromString(Message));
    }

    void AddWarning(const UObject* SourceObject, const FString& Message)
    {
        Warnings.Add(FText::FromString(Message));
    }
};
```

---

## 9. 编译器设计

## 9.1 编译器接口

```cpp
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

---

## 9.2 编译流程

建议编译流程：

1. 检查 Asset 是否存在
2. 找到唯一 `Start`
3. 遍历所有可达节点
4. 分配稳定 `StepIndex`
5. 对所有节点执行 `ValidateNode`
6. 构建 `FNCSkillCompiledStep`
7. 写入 `FNCSkillCompiledSpec`
8. 设置 `bHasCompiledSpec = true`

---

## 9.3 编译器伪代码

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
        OutError = Context.Errors.Num() > 0 ? Context.Errors[0] : FText::FromString(TEXT("Graph validation failed."));
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

---

## 10. 图校验规则

## 10.1 图级校验

编译器需要校验：

- 必须存在且仅存在一个 `Start`
- 至少存在一个 `End`
- 所有节点从 `Start` 可达
- 不允许空节点
- 不允许未知 `StableNodeId`
- 不允许未知 `RegistryName`

## 10.2 节点级校验

每个节点调用自身的 `ValidateNode`，例如：

- `CheckRange.FloatParam0 > 0`
- `ApplyDamage.FloatParam0 >= 0`
- `Start` 必须 1 个出口
- `End` 必须 0 个出口

---

## 11. 节点与共享 schema 对齐方式

## 11.1 原则

每个 UE 节点类都必须和共享 schema 中的节点一一对应。

绑定关系至少包含：

- `StableNodeId`
- `RegistryName`

例如：

| UE 类名 | StableNodeId | RegistryName |
|--------|--------------|--------------|
| `UNCStartSkillNode` | 1 | `Start` |
| `UNCSelectTargetSkillNode` | 2 | `SelectTarget` |
| `UNCCheckRangeSkillNode` | 3 | `CheckRange` |
| `UNCApplyDamageSkillNode` | 4 | `ApplyDamage` |
| `UNCEndSkillNode` | 5 | `End` |

---

## 11.2 参数槽语义

当前阶段统一使用通用槽位：

- `FloatParam0`
- `FloatParam1`
- `IntParam0`
- `NameParam`
- `StringParam`

参数语义由 schema 定义。

例如：

### CheckRange
- `FloatParam0 -> RequiredRange`

### ApplyDamage
- `FloatParam0 -> BaseDamage`
- `FloatParam1 -> AttackPowerScale`

这样 UE 细节面板可以用 schema 显示真实业务名，而不是直接显示 `FloatParam0`。

---

## 12. UE 编辑器细节面板建议

细节面板不建议直接暴露：

- `FloatParam0`
- `FloatParam1`

而应动态显示 schema 里的名字：

例如 `ApplyDamage` 节点显示：

- `Base Damage`
- `Attack Power Scale`

`CheckRange` 节点显示：

- `Required Range`

实现上可以做一个定制细节面板：

- 读取 `GetRegistryEntry()`
- 遍历 `FloatParam0/FloatParam1/...` 的 schema
- 如果槽位 `Key` 为空则隐藏
- 如果槽位 `bRequired == true`，则在 UI 中标记必填

---

## 13. 推荐辅助接口

## 13.1 schema 查询桥接

建议做一个 UE 侧 bridge：

```cpp
struct FNCSkillNodeSchemaBridge
{
    static const FSkillNodeRegistryEntry* FindByNodeId(uint16 NodeId);
    static const FSkillNodeRegistryEntry* FindByRegistryName(FName RegistryName);
    static FText GetDisplayName(uint16 NodeId);
    static FName GetCategory(uint16 NodeId);
};
```

用途：

- UE 节点显示
- 编辑器面板显示
- 编译前合法性校验
- 节点创建菜单

---

## 13.2 创建节点菜单

节点创建菜单建议从 schema 自动生成，而不是手工维护：

- 遍历所有 `FSkillNodeRegistryEntry`
- 过滤可编辑节点
- 按 Category 分组
- 用 `DisplayName` 生成菜单项
- 创建对应节点类实例

这要求 UE 侧存在一份：

- `RegistryName -> UClass`
- 或 `StableNodeId -> UClass`

映射表。

例如：

```cpp
TSubclassOf<UNCSkillGraphNodeBase> FNCSkillEditorNodeFactory::ResolveNodeClass(FName RegistryName);
```

---

## 14. 服务器兼容要求

UE 编译结果必须保证服务器可直接读到：

- `CompiledSpec.SkillId`
- `CompiledSpec.CooldownSeconds`
- `CompiledSpec.CastTimeSeconds`
- `CompiledSpec.MaxRange`
- `CompiledSpec.TargetType`
- `CompiledSpec.Steps`
- 每个 Step 的 `NodeId / NodeType / NextStepIndices / ParamSlots`

服务器不应依赖：

- UE 节点对象类名
- 编辑器专用字段
- 图布局信息
- EditorOnly 数据

---

## 15. 最终建议

推荐实际落地顺序：

### 第一阶段
先实现最小骨架：

- `UNCSkillGraphAsset`
- `UNCSkillGraphNodeBase`
- 5 个基础节点
- `FNCSkillCompiledStep`
- `FNCSkillCompiledSpec`
- `FNCSkillGraphCompiler`

### 第二阶段
完善编辑器体验：

- 节点面板
- 节点分类菜单
- schema 驱动参数 UI
- 编译按钮
- 编译错误面板

### 第三阶段
升级协议：

- 编译结果稳定写入 `NodeId`
- 服务器优先按 `NodeId` 识别节点
- 增加更多节点类型

---

## 16. 一句话结论

UE 侧应新增：

- `UNCSkillGraphNodeBase`

UE 节点负责：

- 编辑
- 校验
- 编译

服务器继续只负责：

- 读取 `CompiledSpec`
- 转换成 `FSkillSpec`
- 执行 `FSkillStep`

这是最稳妥、最容易扩展的结构。
