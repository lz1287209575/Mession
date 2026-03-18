# Module: Protocol

这份文档只做协议层的模块级补充说明。

## 当前结构

项目当前协议大致分两层：

- `Client <-> Gateway`
- `Server <-> Server`

## 当前结论

客户端正式入口已经收口到统一函数调用。  
跨服仍使用项目内部消息与 RPC 机制。

如果要理解正式协议方向，优先看：

- [client-unified-function-call.md](/workspaces/Mession/Docs/client-unified-function-call.md)
- [function-id-rules.md](/workspaces/Mession/Docs/function-id-rules.md)
- [validation.md](/workspaces/Mession/Docs/validation.md)
