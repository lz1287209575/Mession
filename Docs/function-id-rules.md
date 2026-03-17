# Function ID Rules

## Goal

这份文档定义 Mession 当前 `FunctionID` 的生成与使用规则。

目标不是让业务层手工维护 `FunctionID`，而是明确：

- 业务层只维护一套函数声明
- `FunctionName` 是逻辑唯一标识
- `FunctionID` 只是传输层/运行时使用的自动派生值
- UE / Client / Server 都必须遵守同一套生成规则

## Core Principle

`FunctionID` **不能**成为一张人工维护的协议表。

唯一事实来源应当是函数声明本身，例如：

```cpp
MFUNCTION(Client, Message=MT_Chat, Route=RouterResolved, Target=World, Auth=Required, Wrap=PlayerClientSync)
void Client_Chat(uint64 ClientConnectionId, const SClientChatPayload& ChatPayload);
```

这里真正需要人维护的只有：

- 函数所属作用域
- 函数名
- 参数类型
- 路由/鉴权/包装等策略

`FunctionID` 必须由工具链或稳定算法自动导出，不能额外手填。

## Current Canonical Rule

当前服务端稳定 ID 生成规则定义在：

- [Reflection.h](/workspaces/Mession/Source/NetDriver/Reflection.h#L95)

当前规则为：

- 输入：`ScopeName` + `"::"` + `MemberName`
- 算法：`FNV-1a 32-bit`
- 输出：将 32-bit hash 折叠为 16-bit
- 特殊值：若结果为 `0`，强制改为 `1`

等价伪代码：

```text
Hash = 2166136261
for ch in ScopeName:
  Hash = (Hash xor uint8(ch)) * 16777619

Hash = (Hash xor ':') * 16777619
Hash = (Hash xor ':') * 16777619

for ch in MemberName:
  Hash = (Hash xor uint8(ch)) * 16777619

Folded = ((Hash >> 16) xor (Hash & 0xFFFF)) & 0xFFFF
if Folded == 0:
  Folded = 1
return Folded
```

## What Counts As ScopeName

当前仓库里不同场景的 `ScopeName` 约定如下：

- 反射函数注册：使用 `ClassName`
- RPC 稳定 ID：使用 `ClassName + FunctionName`
- 枚举稳定 ID：使用固定 scope `"MEnum"` 加枚举名

对 UE / Client / Agent 来说，当前最重要的是 RPC / 客户端入口函数：

- `ScopeName = ClassName`
- `MemberName = FunctionName`

例如：

```text
ScopeName  = "MGatewayServer"
MemberName = "Client_Chat"
```

## UE Rule

UE 层**不手工指定** `FunctionID`。

UE 层应该只提供：

- `ClassName`
- `FunctionName`
- Payload

然后由 UE 侧运行时或生成工具自动计算 `FunctionID`。

推荐调用语义：

```cpp
CallGenerated("MGatewayServer", "Client_Chat", Payload);
```

底层自动完成：

1. 用与服务端一致的算法计算 `FunctionID`
2. 按统一规则序列化 payload
3. 发出网络包

## What UE Must Not Do

UE 侧不要采用以下做法：

- 手写 `FunctionID = 123`
- 维护一张 `FunctionName -> FunctionID` 的人工配置表
- 单独维护一套和服务端不同的编号分配逻辑
- 在 Blueprint / DataAsset / 配置表中把 `FunctionID` 当业务字段长期保存

这些做法都会把“只维护一套声明”的目标重新打散成两套系统。

## Stability Contract

下面这些变化会导致 `FunctionID` 变化：

- 改 `ClassName`
- 改 `FunctionName`
- 改大小写
- 改 namespace/owner 语义并映射到不同 `ClassName`

下面这些变化在当前规则下**不会**改变 `FunctionID`：

- 修改参数名
- 修改函数体实现
- 修改函数 metadata
- 修改路由策略（前提是不改 `ClassName` / `FunctionName`）

因此，`ClassName + FunctionName` 应视为协议稳定面的一部分。

如果函数已经对外使用，重命名就不再只是“代码重构”，而是协议变更。

## Collision Rule

当前 ID 宽度是 `uint16`，因此理论上存在碰撞可能。

当前服务端已有基础碰撞告警：

- [Reflection.h](/workspaces/Mession/Source/NetDriver/Reflection.h#L447)

当前约束：

- 发现碰撞必须视为协议问题处理，而不是忽略告警继续使用
- 不允许靠人工改 UE 侧编号来“修碰撞”
- 若碰撞逐渐成为现实问题，应升级到底层方案，例如扩大 ID 位宽或引入生成 manifest 校验

## Recommended Long-Term Model

长期建议模型如下：

### Authoring Layer

业务同学只写一套函数声明：

```cpp
MFUNCTION(Client, Route=RouterResolved, Target=World, Auth=Required)
void Client_PlayerMove(...);
```

或者在 UE 侧写对应声明：

```cpp
UFUNCTION(BlueprintCallable)
void Client_PlayerMove(...);
```

业务侧只理解：

- 函数名
- 参数
- 路由策略

不需要理解协议号。

### Build/Codegen Layer

工具链负责自动导出：

- `FunctionName -> FunctionID`
- payload bind/decode
- 可调用 manifest
- 路由/鉴权策略

### Transport Layer

传输层使用自动生成的 `FunctionID`，而不是人工维护的协议号。

也就是说：

- 逻辑唯一标识：`FunctionName`
- 传输唯一标识：自动派生的 `FunctionID`

## When To Use MessageType

当前仓库里 `Client <-> Gateway` 还保留 `MessageType + Payload` 形式。

这是现阶段兼容路径，不是长期必须保留的业务心智模型。

长期如果客户端入口也切到“统一函数声明驱动”，则建议：

- 业务层不再显式感知 `MessageType`
- `MessageType` 若继续保留，也只作为底层兼容编码细节
- 新能力优先围绕统一函数声明扩展，而不是继续增加手工枚举项

## Implementation Checklist For UE

UE 对接时建议遵守下面的最小规则：

- [ ] 复用与服务端一致的 `ComputeStableReflectId` 算法
- [ ] 输入严格使用服务端约定的 `ClassName` 与 `FunctionName`
- [ ] 不手写 `FunctionID`
- [ ] 不维护人工映射表
- [ ] 发送前先按统一 payload 规则编码
- [ ] 为关键函数补一条双端一致性校验，确认 UE 算出的 ID 与服务端一致

## Current References

- [Reflection.h](/workspaces/Mession/Source/NetDriver/Reflection.h#L95)
- [Reflection.h](/workspaces/Mession/Source/NetDriver/Reflection.h#L447)
- [Reflection.h](/workspaces/Mession/Source/NetDriver/Reflection.h#L1471)
- [ServerRpcRuntime.cpp](/workspaces/Mession/Source/Common/ServerRpcRuntime.cpp#L210)
- [client-protocol-reflection.md](/workspaces/Mession/Docs/client-protocol-reflection.md)
