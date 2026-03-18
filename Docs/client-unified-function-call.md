# Client Unified Function Call

这份文档定义 Mession 当前客户端正式入口模型。

## 当前结论

`Client <-> Gateway` 的正式方向已经确定为：

- 传输类型：`MT_FunctionCall`
- 逻辑标识：`FunctionName`
- 传输标识：自动生成的 `FunctionID`
- Gateway 行为：decode -> auth -> route -> invoke

## 目标

这套模型要解决的问题是：

- 不再继续扩张手写客户端消息号
- 不再把 Gateway 做成业务 `switch` 中心
- 让 Client / Gateway / World 使用同一套函数语义

## Wire Format

当前统一函数调用包的关键字段是：

- `MsgType = MT_FunctionCall`
- `FunctionID`
- `PayloadSize`
- `Payload`

具体 `FunctionID` 规则见 [function-id-rules.md](/workspaces/Mession/Docs/function-id-rules.md)。

## Gateway 责任

Gateway 收到统一函数调用后，负责：

1. 基础解码
2. 查找生成的 manifest
3. 执行鉴权检查
4. 判断本地处理还是跨服路由
5. 把 payload 交给目标处理者

Gateway 不负责：

- 承载 World 业务规则
- 人工维护函数表
- 为每条客户端消息手写专属 glue

## 兼容策略

当前结论是：

- 正式入口已经收口到 `MT_FunctionCall`
- 历史入口不再作为长期扩展方向
- 新能力默认不再增加新的客户端业务消息号

## 当前配套

这套方案当前已经和下列内容配套：

- `FunctionID` 规则
- UE 下行统一函数调用
- `validate.py` 正向与负向验证

## 与反射体系的关系

客户端协议逐步收口到函数声明驱动的原因是：

- 业务语义和传输语义不应长期分裂
- Client 和 Server 不应各维护一份协议映射表
- Gateway 不应继续长成大 `switch`

因此当前推荐是：

- 业务层维护函数声明
- 传输层维护自动生成的 `FunctionID`
- Gateway 按生成结果做 decode / invoke / route

相关文档：

- [function-id-rules.md](/workspaces/Mession/Docs/function-id-rules.md)
- [ue-gateway-quickstart.md](/workspaces/Mession/Docs/ue-gateway-quickstart.md)
- [ue-client-downlink-function-call.md](/workspaces/Mession/Docs/ue-client-downlink-function-call.md)
