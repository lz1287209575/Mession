# Client Protocol Reflection

这份文档说明 Mession 为什么把客户端协议逐步收口到反射与函数声明驱动。

## 当前结论

客户端协议的长期方向不是继续维护大量人工消息号，
而是：

- 业务层维护函数声明
- 传输层维护自动生成的 `FunctionID`
- Gateway 按生成结果做 decode / invoke / route

## 这样做的原因

继续维持 `MessageType + PayloadStruct` 的问题是：

- 业务语义和传输语义分裂
- 客户端和服务端都要维护映射表
- Gateway 很容易重新长成一个大 `switch`

函数调用模型的好处是：

- 逻辑标识统一成 `FunctionName`
- `FunctionID` 规则统一
- 生成链路可以收敛 glue 代码

## 推荐声明方式

对客户端正式入口，推荐继续走：

```cpp
MFUNCTION(Client, Message=MT_FunctionCall, Route=RouterResolved, Target=World, Auth=Required)
void Client_PlayerMove(...);
```

核心点不是示例里的元数据长什么样，
而是客户端正式入口应该由函数声明驱动，而不是手工协议表驱动。

## 边界

这份文档只讨论客户端入口如何和反射体系对齐，不讨论：

- 具体 UE 实现细节
- DB 持久化
- Gameplay 对象模型

这些分别见：

- [ue-gateway-quickstart.md](/workspaces/Mession/Docs/ue-gateway-quickstart.md)
- [ue-client-downlink-function-call.md](/workspaces/Mession/Docs/ue-client-downlink-function-call.md)
- [gameplay-avatar-framework.md](/workspaces/Mession/Docs/gameplay-avatar-framework.md)
