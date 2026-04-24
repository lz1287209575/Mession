# Validation Protocol Schema

## 目标

`validate` 的长期方向不是继续扩张一组手写 `parse_*_response(...)`，而是消费一份由代码生成阶段导出的协议 Schema，然后通过通用 decoder 解析线协议 payload。

这份文档定义 validate 工具侧期望消费的 Schema 形态，以及当前推荐的最小字段集合。

## 设计原则

- Schema 描述的是线协议布局，不是 C++ 内存对象语义
- Python 侧不猜 native padding、对齐规则或编译器 ABI
- 如果线协议里确实有 padding，必须在 Schema 中显式写出
- 嵌套结构、向量、字符串都由 Schema 显式描述
- validate 只依赖生成出的 Schema 文件，不直接绑定 C++ 运行时反射

## 生成与消费

### 期望生成位置

第一版建议由 `MHeaderTool` 或后续独立导出步骤生成：

```text
Build/Generated/ValidationProtocolSchema.json
```

在真实生成文件落地前，validate 工具允许加载一份仓库内置的最小兼容 Schema：

```text
Scripts/validation/schemas/compat_protocol_schema.json
```

它只用于迁移期跑通最小闭环，不能替代正式生成产物。

### 消费位置

- `Scripts/validation/schema_loader.py`
- `Scripts/validation/transport/reflection_decoder.py`

## 根结构

建议的 JSON 根结构：

```json
{
  "schema_version": 1,
  "generated_at": "2026-04-24T08:00:00Z",
  "producer": "MHeaderTool",
  "structs": {
    "FPlayerQueryProfileResponse": {
      "fields": [
        { "name": "bSuccess", "kind": "bool" },
        { "name": "PlayerId", "kind": "u64" },
        { "name": "CurrentSceneId", "kind": "u32" },
        { "name": "Gold", "kind": "u32" },
        { "name": "EquippedItem", "kind": "string" },
        { "name": "Level", "kind": "u32" },
        { "name": "Experience", "kind": "u32" },
        { "name": "Health", "kind": "u32" },
        { "name": "Error", "kind": "string" }
      ]
    }
  }
}
```

其中：

- `schema_version`
  - Schema 自身版本
- `generated_at`
  - 生成时间
- `producer`
  - 生成工具名
- `structs`
  - 以结构名为 key 的结构定义表

## 字段定义

### 通用字段结构

```json
{
  "name": "PlayerId",
  "kind": "u64"
}
```

### 支持的 `kind`

- `bool`
- `u8`
- `u16`
- `u32`
- `u64`
- `f32`
- `string`
- `struct`
- `vector`
- `bytes`
- `padding`

## 各类型约定

### 原始标量

示例：

```json
{ "name": "Health", "kind": "u32" }
{ "name": "X", "kind": "f32" }
```

### 字符串

当前约定：

- `u32` 长度
- 紧随其后的 UTF-8 字节

示例：

```json
{ "name": "Error", "kind": "string" }
```

### 嵌套结构

示例：

```json
{
  "name": "MonsterUnit",
  "kind": "struct",
  "type_name": "FCombatUnitRef"
}
```

### 向量

建议统一为：

- `u32` 元素个数
- 后跟元素序列

示例：

```json
{
  "name": "MemberPlayerIds",
  "kind": "vector",
  "item_kind": "u64"
}
```

嵌套结构向量示例：

```json
{
  "name": "Members",
  "kind": "vector",
  "item_kind": "struct",
  "item_type_name": "FPartyMember"
}
```

### 固定字节块

如果某段协议是固定大小原始字节，可以使用：

```json
{
  "name": "Signature",
  "kind": "bytes",
  "size": 16
}
```

### Padding

像 `FCombatUnitRef` 这种当前按原始 struct layout 序列化、带显式 padding 的类型，不应再由 Python 猜测，需要在 Schema 里写清楚：

```json
{
  "fields": [
    { "name": "UnitKind", "kind": "u8" },
    { "name": "_pad0", "kind": "padding", "size": 7 },
    { "name": "CombatEntityId", "kind": "u64" },
    { "name": "PlayerId", "kind": "u64" }
  ]
}
```

约定：

- `padding` 字段默认不进入 decode 结果
- `name` 仅用于调试和 schema 可读性

## 示例

### `FCombatUnitRef`

```json
{
  "fields": [
    { "name": "UnitKind", "kind": "u8" },
    { "name": "_pad0", "kind": "padding", "size": 7 },
    { "name": "CombatEntityId", "kind": "u64" },
    { "name": "PlayerId", "kind": "u64" }
  ]
}
```

### `FPlayerQueryProgressionResponse`

```json
{
  "fields": [
    { "name": "bSuccess", "kind": "bool" },
    { "name": "PlayerId", "kind": "u64" },
    { "name": "Level", "kind": "u32" },
    { "name": "Experience", "kind": "u32" },
    { "name": "Health", "kind": "u32" },
    { "name": "Error", "kind": "string" }
  ]
}
```

### `FPartyMemberJoinedNotify`

```json
{
  "fields": [
    { "name": "PartyId", "kind": "u64" },
    { "name": "LeaderPlayerId", "kind": "u64" },
    { "name": "JoinedPlayerId", "kind": "u64" },
    {
      "name": "MemberPlayerIds",
      "kind": "vector",
      "item_kind": "u64"
    }
  ]
}
```

## 兼容策略

在 Schema 导出真正落地前：

- 现有手写 `parse_*_response(...)` 仍然允许存在
- 但它们应被视为临时兼容层，而不是最终结构
- 新增协议解析时，优先补 Schema 和通用 decoder，而不是继续新增大量手写 parser

## 当前建议

第一阶段建议先完成：

1. 固化 Schema JSON 格式
2. 在 validate 工具侧实现 `schema_loader` 和通用 decoder 骨架
3. 把现有手写 parser 标记为兼容迁移层
4. 再从 `MHeaderTool` 或协议定义生成阶段补出真实导出文件
