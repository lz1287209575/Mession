# Function ID Rules

这份文档定义 Mession 当前 `FunctionID` 的使用原则。

## 核心原则

`FunctionID` 不是人工维护的协议号表。  
唯一事实来源应该是函数声明本身。

也就是说：

- 业务层维护 `FunctionName`
- 传输层派生 `FunctionID`
- UE / Client / Server 共同遵守同一套生成规则

## 当前规则

当前工程里采用稳定派生规则来生成 `FunctionID`。

要求是：

- 同一函数名在同一规则下得到稳定结果
- 不依赖进程运行时随机值
- 不依赖人工分配表

## 使用约束

- 不手写硬编码 `FunctionID`
- 不在 UE 和 Server 各自维护一份映射表
- 不为了兼容个别客户端临时修改生成规则

## 工程约定

如果要新增客户端或下行函数：

1. 先写函数声明
2. 再让工具链或运行时生成 `FunctionID`
3. 再用脚本和联调验证结果一致

## 相关文档

- [client-unified-function-call.md](/workspaces/Mession/Docs/client-unified-function-call.md)
- [ue-client-downlink-function-call.md](/workspaces/Mession/Docs/ue-client-downlink-function-call.md)
- [ue-gateway-quickstart.md](/workspaces/Mession/Docs/ue-gateway-quickstart.md)
