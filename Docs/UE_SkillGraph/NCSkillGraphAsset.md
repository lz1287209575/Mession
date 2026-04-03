# NCSkillGraphAsset

## 目标

`UNCSkillGraphAsset` 是 UE 侧技能图的资源载体。

它同时保存两类信息：

- 编辑器图数据
- 编译后的 `CompiledSpec`

服务器只消费第二类数据。

## 推荐定义

```cpp
UCLASS(BlueprintType)
class UNCSkillGraphAsset : public UObject
{
    GENERATED_BODY()

public:
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

    UPROPERTY(Instanced)
    TArray<TObjectPtr<class UNCSkillGraphNodeBase>> Nodes;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    bool bHasCompiledSpec = false;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    int32 CompiledSchemaVersion = 1;

    UPROPERTY(VisibleAnywhere, Category="Compiled")
    FNCSkillCompiledSpec CompiledSpec;
};
```

## 字段职责

### 基础技能字段

这些字段同时属于编辑器配置和编译输出来源：

- `SkillId`
- `CooldownSeconds`
- `CastTimeSeconds`
- `MaxRange`
- `TargetType`

编译器在生成 `CompiledSpec` 时应直接从这些字段取值。

### `Nodes`

- 保存整张图的节点对象
- 使用 `Instanced`，确保资源内部持有节点
- 节点之间的边关系由节点本身的 `OutgoingNodes` 表示

### `bHasCompiledSpec`

- 表示当前资源是否已有可用编译结果
- 只要图结构或参数被修改，就建议将其置为 `false`
- 编译成功后再置回 `true`

### `CompiledSchemaVersion`

- 用于后续升级编译格式
- 当 `CompiledStep` 结构有变化时递增

### `CompiledSpec`

- 服务器最终读取的核心数据
- 应避免混入任何 EditorOnly 字段

## 资源生命周期建议

### 创建资源时

- 初始化基础技能字段
- 创建空节点数组
- `bHasCompiledSpec = false`

### 编辑图时

- 节点新增/删除/连线变化/参数变化，都应标记脏状态
- 推荐自动清掉 `bHasCompiledSpec`

### 点击编译时

- 调用 `FNCSkillGraphCompiler`
- 成功则写回 `CompiledSpec`
- 失败则保留旧编译结果或清空，具体取决于项目策略

## 建议辅助接口

```cpp
UFUNCTION(CallInEditor, Category="Skill")
bool CompileSkillGraph();

void MarkCompiledDataDirty();

UNCSkillGraphNodeBase* FindStartNode() const;
```

## `CompileSkillGraph` 建议行为

- 创建编译器
- 调用编译
- 成功则刷新 `CompiledSpec`
- 失败则将错误反馈到日志、通知或 Details 面板

## 图级约束

`UNCSkillGraphAsset` 层建议保证：

- 至少有一个 `Start`
- 至少有一个 `End`
- 所有节点属于同一个 Outer 资源
- 节点 `NodeGuid` 唯一

## 不建议存入 Asset 的内容

- Slate GraphNode 指针
- 编辑器窗口状态
- 临时编译上下文
- 运行时服务器缓存对象

这些都应是临时态，不应写入资源。
