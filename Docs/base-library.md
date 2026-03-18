# Base Library

这份文档简要说明 Mession 当前的基础类型与常用公共能力。

## 类型与容器

项目在 `Core/NetCore.h` 中统一封装常用类型与 STL 别名，例如：

- 整数类型：`uint8` 到 `uint64`、`int8` 到 `int64`
- 字符串：`FString`
- 容器：`TArray`、`TVector`、`TMap`、`TSet`
- 指针：`TSharedPtr`、`TUniquePtr`
- 函数：`TFunction`

新代码建议优先使用项目别名，减少底层类型分裂。

## 常用基础能力

- 时间与休眠：`MTime`
- 唯一 ID：`MUniqueIdGenerator`
- 基础数学：`SVector`、`SRotator`
- 配置：`MConfig`
- 字符串工具：`MString`
- 结果类型：`TResult`

## 当前原则

- 底层通用能力尽量统一从 `Core` 和 `Common` 暴露
- 上层业务不要重复造一套基础工具
- Gameplay / Server 代码尽量直接使用项目别名而不是混杂多套风格
