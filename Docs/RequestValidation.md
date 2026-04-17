# 请求校验约定

这份文档描述当前仓库里 `ServerCall` 请求应该如何写校验。

目标很简单：

- 字段级约束和字段定义写在一起
- 反射系统能直接读到这些约束
- `ServerCallRequestValidation` 只保留少量复杂校验

## 当前默认规则

新增 `ServerCall` 请求时，优先把基础校验写到 `MPROPERTY(Meta=...)` 上，而不是先写一份 `TRequestValidator<>`。

例如：

```cpp
MSTRUCT()
struct FPlayerEquipItemRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerEquipItem"))
    uint64 PlayerId = 0;

    MPROPERTY(Meta=(NonEmpty, ErrorCode="equipped_item_required", ErrorContext="PlayerEquipItem"))
    MString EquippedItem;
};
```

这一类规则已经由 `Source/Servers/App/ServerCallRequestValidation.h` 自动处理，不需要再手写重复的 validator。

## 当前支持的 Meta

- `NonZero`
  - 适用于整数、浮点等数值字段
  - 值为 `0` 时返回错误
- `NonEmpty`
  - 适用于 `MString`
  - 空串时返回错误
- `Required`
  - 当前语义是“非零或非空”
  - 只适合最简单字段，日常更推荐显式写 `NonZero` 或 `NonEmpty`
- `Min`
  - 数值下界，例如 `Meta=(Min=1)`
- `Max`
  - 数值上界，例如 `Meta=(Max=99)`
- `ErrorCode`
  - 校验失败时使用的错误码
- `ErrorContext`
  - 校验失败时写入 `FAppError.Message` 的上下文
- `ErrorMessage`
  - 如果需要覆盖默认上下文，可直接写固定错误消息

## 推荐写法

### 玩家 ID / 场景 ID

```cpp
MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerMove"))
uint64 PlayerId = 0;

MPROPERTY(Meta=(NonZero, ErrorCode="scene_id_required", ErrorContext="PlayerSwitchScene"))
uint32 SceneId = 0;
```

### 字符串输入

```cpp
MPROPERTY(Meta=(NonEmpty, ErrorCode="equipped_item_required", ErrorContext="PlayerEquipItem"))
MString EquippedItem;
```

### 范围字段

```cpp
MPROPERTY(Meta=(Min=1, Max=10, ErrorCode="skill_level_invalid", ErrorContext="UpgradeSkill"))
uint32 SkillLevel = 0;
```

## 什么时候还要写 `TRequestValidator<>`

只有在字段级 Meta 不足以表达规则时，才保留自定义 validator。

典型场景：

- 多个字段之间有组合约束
- 多个字段必须成对出现
- 需要比较两个字段的关系
- 需要根据枚举值决定另一个字段是否必填

例如：

```cpp
template<>
struct TRequestValidator<FExampleRequest>
{
    static TOptional<FAppError> Validate(const FExampleRequest& Request)
    {
        if (Request.PlayerId == Request.TargetPlayerId)
        {
            return FAppError::Make("target_player_invalid", "ExampleCall");
        }
        return std::nullopt;
    }
};
```

这里仍然建议：

- 单字段规则先放到 `MPROPERTY(Meta=...)`
- `TRequestValidator<>` 只补充跨字段逻辑

## 当前项目里的用法样板

- `Source/Servers/World/Player/PlayerInventory.h`
  - `FPlayerQueryInventoryRequest`
- `Source/Protocol/Messages/World/PlayerModifyMessages.h`
- `Source/Protocol/Messages/World/PlayerQueryMessages.h`
- `Source/Protocol/Messages/World/PlayerSocialMessages.h`
- `Source/Protocol/Messages/Combat/CombatWorldMessages.h`

这些文件已经按“字段 Meta 优先”的方式收过一轮，可以直接照着抄。

## 新增接口时的最小流程

1. 先在 request struct 的字段上写 `Meta`
2. 如果还有跨字段关系，再补 `TRequestValidator<>`
3. 编译
4. 跑对应验证套件，至少保证 `runtime_dispatch` 不回归

## Request 定义归属

当前项目对业务 request 的目标是“只保留一份定义”。

也就是说：

- `ClientCall` 不再为普通业务请求单独维护一套 `FClient*Request`
- Player 能力直接使用 `FPlayer*Request`
- World 编排能力直接使用 `FWorld*Request`
- `FClient*` 主要保留给 response、notify 和少数纯客户端语义请求

这样新增接口时，不需要再维护：

- 一份 client request
- 一份 world/player request
- 一层 request 投影

默认只需要维护真正的业务 request 本身。

## 不建议的写法

- 只为了 `PlayerId != 0` 再单独写一份 `TRequestValidator<>`
- 同一类错误码在不同请求里随意换名字
- 字段已经写了 `Meta`，又在 validator 里重复写完全一样的判断

当前方向很明确：

- 请求结构拥有字段约束
- 反射层携带约束元数据
- 校验框架消费元数据
- 自定义 validator 只保留业务组合逻辑
