# NCSkillGraphNodeBase

## 目标

`UNCSkillGraphNodeBase` 是 UE 技能图节点的抽象基类。

它的职责：

- 表示一个可编辑节点
- 提供节点身份和显示信息
- 持有通用参数槽
- 管理出边
- 做节点级校验
- 将自己编译为 `FNCSkillCompiledStep`

它不负责：

- 服务端技能执行
- 实时战斗结算
- 网络协议处理

## 推荐定义

```cpp
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class UNCSkillGraphNodeBase : public UObject
{
    GENERATED_BODY()

public:
    virtual uint16 GetStableNodeId() const PURE_VIRTUAL(UNCSkillGraphNodeBase::GetStableNodeId, return 0;);
    virtual FName GetRegistryName() const PURE_VIRTUAL(UNCSkillGraphNodeBase::GetRegistryName, return NAME_None;);

    virtual FText GetNodeDisplayName() const;
    virtual FName GetNodeCategory() const;

    virtual int32 GetMinOutgoingEdges() const;
    virtual int32 GetMaxOutgoingEdges() const;

    virtual void ValidateNode(struct FNCSkillCompileContext& Context) const;

    virtual bool BuildCompiledStep(
        struct FNCSkillCompileContext& Context,
        struct FNCSkillCompiledStep& OutStep) const PURE_VIRTUAL(UNCSkillGraphNodeBase::BuildCompiledStep, return false;);

public:
    UPROPERTY()
    FGuid NodeGuid;

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

    UPROPERTY()
    TArray<TObjectPtr<UNCSkillGraphNodeBase>> OutgoingNodes;
};
```

## 默认实现建议

## schema 查询

建议基类内部通过共享 schema 查配置，而不是每个子类重复写：

```cpp
const FSkillNodeRegistryEntry* GetRegistryEntry() const;
```

默认行为可以从 schema 派生：

- `DisplayName`
- `Category`
- `MinOutgoingEdges`
- `MaxOutgoingEdges`

## 默认校验

建议基类的 `ValidateNode` 负责：

- 校验 schema 项存在
- 校验出边数量是否满足约束
- 校验出边目标不为空

节点特化校验交给子类，例如：

- `CheckRange` 校验 `FloatParam0 > 0`
- `ApplyDamage` 校验伤害参数范围

## 通用参数槽策略

当前阶段建议保留固定五个通用槽：

- `FloatParam0`
- `FloatParam1`
- `IntParam0`
- `NameParam`
- `StringParam`

原因：

- 与当前服务器解析器兼容
- 与共享 schema 已对齐
- 便于受限版 `.uasset` 解析
- 可以通过 Details 自定义把槽位渲染成真实业务名

## 细节面板建议

不要直接向策划展示 `FloatParam0` 这种名字。

应按 schema 显示为：

- `Required Range`
- `Base Damage`
- `Attack Power Scale`

实现方式：

- 读取当前节点的 `FSkillNodeRegistryEntry`
- 如果某个槽位 `Key` 为空，则隐藏
- 如果槽位 `bRequired == true`，则标记必填

## 派生类最小集合

建议先做 5 个：

- `UNCStartSkillNode`
- `UNCSelectTargetSkillNode`
- `UNCCheckRangeSkillNode`
- `UNCApplyDamageSkillNode`
- `UNCEndSkillNode`

每个子类只需要最少覆写：

- `GetStableNodeId`
- `GetRegistryName`
- 必要时覆写 `ValidateNode`
- 实现 `BuildCompiledStep`

## 示例：ApplyDamage

```cpp
UCLASS()
class UNCApplyDamageSkillNode : public UNCSkillGraphNodeBase
{
    GENERATED_BODY()

public:
    virtual uint16 GetStableNodeId() const override { return 4; }
    virtual FName GetRegistryName() const override { return TEXT("ApplyDamage"); }

    virtual void ValidateNode(FNCSkillCompileContext& Context) const override;

    virtual bool BuildCompiledStep(
        FNCSkillCompileContext& Context,
        FNCSkillCompiledStep& OutStep) const override;
};
```

## 示例：BuildCompiledStep

```cpp
bool UNCApplyDamageSkillNode::BuildCompiledStep(
    FNCSkillCompileContext& Context,
    FNCSkillCompiledStep& OutStep) const
{
    OutStep.StepIndex = Context.AllocateStepIndex(this);
    OutStep.NodeId = GetStableNodeId();
    OutStep.NodeType = GetRegistryName();
    OutStep.NextStepIndices = Context.ResolveOutgoingStepIndices(this);
    OutStep.FloatParam0 = FloatParam0;
    OutStep.FloatParam1 = FloatParam1;
    return true;
}
```

## 不建议做的事

- 不在节点类里写服务端伤害公式
- 不在节点类里访问实时世界状态
- 不在节点类里做 RPC
- 不让节点类直接决定服务器执行分支逻辑

节点类的职责应始终保持为“编辑器对象 + 编译源”。
