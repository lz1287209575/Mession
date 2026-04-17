# MObject 内容资产序列化设计

这份文档定义一版可落地的 `MObject` 内容资产方案，目标是把：

- 运行时状态快照
- 可编辑的内容配置
- 运行时高效加载的二进制资源

三件事彻底分开。

## 目标

这版设计解决的是“内容资产”问题，不是继续扩展当前的玩家状态快照。

目标如下：

1. 编辑侧保留一份人类可读的 `JSON`
2. 运行时只加载紧凑二进制
3. 资产内容仍然建立在现有 `MObject + Reflection` 体系上
4. 二进制格式必须稳定，不能依赖运行时自增 ID
5. 资产加载要能从数据创建对象树，而不是只对现有对象做回放

## 当前代码事实

当前仓库已经有一套反射快照能力，但它更接近“状态序列化”，不是“内容资产序列化”。

相关代码：

- 反射对象与属性元数据：`Source/Common/Runtime/Reflect/Reflection.h`
- 对象快照读写：`Source/Common/Runtime/Reflect/Class.cpp`
- 属性值归档：`Source/Common/Runtime/Reflect/Property.cpp`
- 域快照工具：`Source/Common/Runtime/Object/ObjectDomainUtils.h`
- 持久化消费入口：`Source/Common/Runtime/Persistence/PersistenceSubsystem.h`
- 登录时回放玩家对象树：`Source/Servers/World/Player/PlayerEnter.cpp`
- 登出时导出玩家对象树：`Source/Servers/World/Player/PlayerLogout.cpp`

当前这套机制的边界：

1. 快照是“对现有对象写字段数据”
2. 回放是“按 `ObjectPath` 找现有对象再写回字段”
3. 当前 `ClassId` / `PropertyId` 来自运行时自增，不稳定，不适合作为资产格式主键
4. 当前 `MReflectArchive` 更偏内存快照归档，不是一个面向长期兼容的资产文件协议

结论很明确：

- 现有快照系统继续服务 `Persistence` / `Replication`
- 内容资产需要单独定义一套 `Asset` 域和文件格式

## 总体方案

`MObject` 资产采用双格式：

- `JSON`：编辑格式、审查格式、调试格式
- `MOB` 二进制：运行时加载格式

推荐数据流：

```text
MObject Editor / 手写 JSON
    -> .mobj.json
    -> MObjectAssetCompiler
    -> .mob
    -> Runtime Loader
    -> MObject Tree
```

同时保留一条调试链：

```text
Runtime MObject <-> Debug JSON
```

这里不让 `JSON` 直接进游戏主流程。正式运行时只吃 `.mob`。

## 设计原则

### 1. 资产序列化和状态快照分离

二者都叫序列化，但职责不同：

- 资产序列化：用于“创建对象”
- 状态快照：用于“保存对象当前状态”

不要把内容资产格式直接复用成持久化格式，也不要把当前快照格式直接当内容资产格式。

### 2. 资产只序列化显式声明的 `Asset` 域

现有域只有：

- `Replication`
- `Persistence`

内容资产需要新增第三个域：

- `Asset`

只有打了 `Asset` 域的属性，才参与资产导入、导出、编译和运行时加载。

`Edit` 只表示编辑器可见，不等于一定进入资产。

建议约束：

- `Edit + Asset`：可编辑且进入资产
- `Asset` 但非 `Edit`：参与资产，但编辑器默认隐藏或只读
- 仅 `Persistence` / `Replication`：运行时状态字段，不进入内容资产

### 3. 资产协议必须使用稳定 Schema ID

当前的 `ClassId` / `PropertyId` 是运行时注册时自增出来的，跨进程、跨模块、跨构建都不稳定。

因此资产协议不能直接写当前的：

- `MClass::GetId()`
- `MProperty::PropertyId`

这版设计要求新增稳定 ID：

- `AssetTypeId`：稳定类型 ID
- `AssetFieldId`：稳定字段 ID

建议生成规则：

- `AssetTypeId = Hash32("ClassName")`
- `AssetFieldId = Hash32("ClassName.PropertyName")`

并在类注册完成后做碰撞检查。只要碰撞，就在启动时直接报错。

这样二进制里不用存类名和字段名，仍然能保持稳定和紧凑。

### 4. 资产图先限制为“单文件单根对象树”

为了先把第一版做稳，`v1` 只支持：

- 一个资产文件对应一个根 `MObject`
- 对象之间是单根树结构
- 子对象来源于 `Instanced` 对象属性或 `Instanced` 对象数组
- 非 `Instanced` 的 `MObject*` 字段按“引用”处理，不负责拥有关系

这意味着 `v1` 不支持通用跨资产对象图。跨资产引用可以留到后续版本。

### 5. 加载采用“两阶段”

资产加载分两步：

1. 先创建整棵对象树
2. 再填充值并解析引用

这样可以解决前向引用、环引用和父子顺序依赖问题。

## 新增元数据约束

### 属性域

在 `EPropertyDomainFlags` 中新增：

- `Asset = 1 << 2`

### 对象属性语义

`EPropertyType::Object` 在资产里分两种语义：

1. `Instanced`
   - 表示拥有的子对象
   - 编译时内联写入当前资产
   - 加载时创建为 `Outer` 子对象
2. 非 `Instanced`
   - 表示引用
   - `v1` 只允许引用同一资产内的其他对象
   - 二进制里保存目标对象的本地 `ObjectIndex`

### 建议新增的可选 Metadata

这部分不是第一步必须实现，但建议预留：

- `AssetRequired=true`
  - 编译时不能为空
- `AssetInline=true`
  - 对 struct / object 强制内联展开
- `AssetReadOnly=true`
  - 编辑器可见但不可改
- `AssetCategory=Combat/AI/...`
  - 给编辑器分组显示用

## JSON 格式

推荐文件后缀：

- `*.mobj.json`

`JSON` 是权威编辑格式。建议结构如下：

```json
{
  "$class": "MMonsterConfig",
  "$name": "Slime",
  "$id": "root",
  "DisplayName": "Slime",
  "Level": 3,
  "BaseHp": 120,
  "AggroRadius": 800.0,
  "Brain": {
    "$class": "MMonsterBrainConfig",
    "$name": "Brain",
    "$id": "brain",
    "TickInterval": 0.5
  },
  "Skills": [
    {
      "$class": "MMonsterSkillConfig",
      "$name": "Bite",
      "$id": "skill_bite",
      "SkillId": 1001,
      "CooldownMs": 3000
    }
  ],
  "DefaultTarget": {
    "$ref": "#/brain"
  }
}
```

字段规则：

- `$class`
  - 必填
  - 对应反射类名
- `$name`
  - 可选但建议填写
  - 用作对象名，也参与生成对象路径
- `$id`
  - 资产内稳定锚点，供 `$ref` 使用
- `$ref`
  - 资产内对象引用，`v1` 使用 `#/object_id` 语法
- 普通字段
  - 直接按属性名展开
- `Instanced` 对象字段
  - 直接嵌套一个对象节点
- `TVector<InstancedObject>`
  - 展开为数组
- struct
  - 展开为普通 JSON object
- enum
  - 默认写字符串名，不写底层整数

`v1` 的 JSON 不做通用 `$children`。对象树来源必须可从反射属性推导出来，这样编译器和编辑器边界最清楚。

## 二进制格式

推荐文件后缀：

- `*.mob`

`v1` 的目标不是极限压缩，而是：

- 比 JSON 更小
- 比当前快照更稳定
- 可以跳过未知字段
- 可以稳定做版本演进

### 字节序

资产文件统一使用 `little-endian`。

原因：

- 文件格式必须与平台内存布局解耦
- 绝大部分编辑和运行环境都是 little-endian
- 只要协议写死字节序，未来迁移和调试成本最低

这和网络协议是否使用大端是两回事，不冲突。

### 数值编码

建议：

- 定长标量：固定宽度编码
- 长度字段 / 计数字段 / 索引字段：`varuint`
- 有符号整型：`zigzag + varuint`
- 字符串：`varuint length + UTF-8 bytes`

### 文件布局

```text
FileHeader
StringTable
ObjectTable
```

#### FileHeader

固定头：

- `Magic[4] = 'MOBJ'`
- `Version u16 = 1`
- `Flags u16`
- `StringCount varuint`
- `ObjectCount varuint`
- `RootObjectIndex varuint`

#### StringTable

字符串去重表，保存：

- 对象名
- 可选调试字符串
- 未来扩展字段

说明：

- 类名和字段名不进入字符串表主路径，二进制主路径依赖稳定 ID
- 如果需要 debug build，可通过 `Flags` 开启类名/字段名附带导出

#### ObjectTable

每个对象记录：

- `ObjectIndex varuint`
- `ParentObjectIndex varuint`，根对象父索引为 `0`
- `NameStringIndex varuint`，无名对象为 `0`
- `AssetTypeId u32`
- `FieldCount varuint`
- `FieldRecords[]`

每个字段记录：

- `AssetFieldId u32`
- `ValueType u8`
- `PayloadSize varuint`
- `Payload bytes`

这里保留 `PayloadSize`，原因很直接：

- 允许跳过未知字段
- 允许字段类型升级后做兼容读取
- 便于调试器扫描和校验

相比完全无 tag 的顺排字段，多一点开销，但换来的是内容资产需要的版本容错能力。

### 值编码规则

#### 标量

- `bool`：`u8`
- `float` / `double`：固定宽度
- `enum`：底层整数
- `string`：`varuint length + bytes`

#### struct

struct 使用“内嵌字段块”编码：

- 不按裸内存拷贝
- 递归按反射字段写入
- 同样使用稳定 `AssetFieldId`

这样可以避免结构体内存布局变化直接破坏文件兼容性。

#### `Instanced` 对象

`Instanced` 字段不在当前字段 payload 里完整展开对象，而是：

- 在 `ObjectTable` 里为它单独分配 `ObjectRecord`
- 当前字段 payload 只写目标 `ObjectIndex`

这样对象树结构和字段引用结构是统一的。

#### 对象引用

非 `Instanced` 的 `Object` 字段：

- `0` 表示空引用
- 非 `0` 表示目标 `ObjectIndex`

`v1` 限制引用目标必须在同一资产内。

#### 数组

数组编码：

- `ElementCount varuint`
- 逐元素写入

对于标量数组可以后续再补“紧凑连续块优化”，但 `v1` 先不把格式做复杂。

## 加载流程

运行时加载 `.mob` 的标准流程：

1. 读取文件头并校验 `Magic/Version`
2. 读取字符串表
3. 预扫描 `ObjectTable`
4. 第一阶段：按 `AssetTypeId` 创建全部 `MObject`
5. 按 `ParentObjectIndex` 建立 `Outer` 关系和对象名
6. 第二阶段：按字段记录写入 `Asset` 域属性
7. 解析对象引用
8. 执行可选的 `PostLoad` / `ValidateAsset` 钩子

这里最关键的是第 4 步和第 6 步分开，不在读到对象时立刻把所有引用硬写进去。

## 编译流程

`MObjectAssetCompiler` 的标准流程：

1. 读取 `JSON`
2. 根据 `$class` 创建一棵临时 `MObject` 树
3. 只允许写入 `Asset` 域属性
4. 校验类型、必填字段、枚举值、引用目标
5. 为每个对象分配稳定的 `ObjectIndex`
6. 收集字符串表
7. 写出 `.mob`
8. 可选导出一份 pretty JSON 作为规范化结果

建议编译期就做失败即中止，不要把坏资产拖到运行时。

## 运行时与编辑器职责边界

### 编辑器

编辑器只负责：

- 修改 JSON 视图或结构化 DOM
- 调用编译器生成 `.mob`
- 根据反射元数据生成表单或树形编辑界面

编辑器不直接承担运行时对象加载职责。

### 运行时

运行时只负责：

- 加载 `.mob`
- 创建对象树
- 提供只读或受控修改的配置对象

如果运行时需要导出 JSON，只作为调试工具，不反向作为正式存档格式。

## 与现有快照系统的关系

这版设计明确要求两套链路并存：

### 资产链路

- 面向配置
- 输入：`JSON`
- 输出：`.mob`
- 加载结果：新建的 `MObject` 内容树
- 域：`Asset`

### 快照链路

- 面向运行时状态
- 输入/输出：反射快照字节
- 作用：持久化、复制、状态同步
- 域：`Persistence` / `Replication`

不要把 `Persistence` 的 dirty / snapshot 规则直接拿来定义内容文件。

## 建议模块拆分

建议新增一个独立目录：

- `Source/Common/Runtime/Asset/`

`v1` 可以拆成下面几个组件：

1. `MObjectAssetSchema`
   - 生成和查询 `AssetTypeId` / `AssetFieldId`
   - 碰撞检查
2. `MObjectAssetJson`
   - `MObject <-> JSON`
   - 只处理 `Asset` 域
3. `MObjectAssetBinary`
   - `.mob` 读写
   - 基础 varuint / zigzag / chunk 编码
4. `MObjectAssetCompiler`
   - `JSON -> 临时对象树 -> .mob`
5. `MObjectAssetLoader`
   - `.mob -> MObject 树`
6. `MObjectAssetValidator`
   - 必填、类型、引用、重复 ID、非法字段校验

## 第一阶段实现范围

为了尽快让战斗系统能开始接配置，第一阶段建议只支持：

1. 单根对象资产
2. 标量类型
3. enum
4. struct
5. `Instanced` 单对象
6. `TVector` 标量 / struct / `InstancedObject`
7. 同资产内对象引用
8. JSON 导入
9. 二进制编译和运行时加载

第一阶段先不做：

- 跨资产引用
- 差量 patch
- 二进制增量更新
- 通用循环依赖图
- 编辑器 UI 细节
- XML 支持

`XML` 不是实现难点，但也没有明显收益，应该放在很后面。

## 兼容性策略

`v1` 从一开始就按“可演进格式”做，不走一次性顺排内存 dump。

建议规则：

1. 文件头带 `Version`
2. 字段记录带 `AssetFieldId + PayloadSize`
3. 未识别字段直接跳过
4. 缺失字段走类默认值
5. 编译器负责把旧 JSON 规范化到当前结构

这样后续新增字段、改默认值、下线字段都会容易得多。

## 对战斗系统的直接价值

按这版设计推进，战斗系统会得到一条很清楚的内容路径：

- `MonsterConfig`、`SkillConfig`、`BuffConfig` 这类内容对象都可以变成 `MObject`
- 配置编辑阶段看 `JSON`
- 服务器运行时加载 `.mob`
- 运行时状态仍然走当前的 `Persistence/Replication`

这样 `Monster` 本体、`MonsterFactory`、配置对象、运行时实例对象就能彻底分层：

- 配置对象：`Asset`
- 运行时实例：`Persistence/Replication`
- 构造关系：`Factory`

## 推荐落地顺序

建议按下面顺序实施：

1. 给反射系统补 `Asset` 域
2. 给类和属性补稳定 `AssetTypeId` / `AssetFieldId`
3. 先实现 `MObject -> JSON` 导出，确认数据模型
4. 再实现 `JSON -> 临时 MObject 树`
5. 最后实现 `.mob` 编译和加载
6. 用 `MonsterConfig` 做第一批样板资产

## 最小验证集

这套设计落地后，至少要有下面这些自动验证：

1. `JSON -> .mob -> Load -> Export JSON` 结果稳定
2. 新增字段后，旧 `.mob` 仍能加载
3. 未知字段能被跳过
4. 缺失字段能回退默认值
5. 同资产对象引用能正确解析
6. `Instanced` 子对象能正确恢复 `Outer/Name`
7. 非 `Asset` 域字段不会被误写入资产

## 最后的取舍

这版设计故意没有选“最省几个字节”的极限格式，而是选了“稳定、紧凑、可演进”的平衡点。

原因很简单：

- 对内容资产来说，稳定和可升级比省掉几字节更重要
- 对运行时来说，二进制读取已经比 JSON 足够高效
- 对工程演进来说，稳定 Schema ID 和两阶段加载是必须先打牢的基础

如果后续需要，可以在不改模型的前提下继续加：

- 标量数组块压缩
- 字符串表压缩
- 分 chunk 懒加载
- 跨资产引用表
- 资源包批量编译
