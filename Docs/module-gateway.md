# Module: Gateway

`Source/Servers/Gateway` 是客户端唯一正式入口。

## 当前职责

- 客户端连接接入
- 协议解码
- 鉴权前后状态管理
- 路由查询
- 向 Login / World 转发

## 当前原则

Gateway 不承载 World 业务规则。  
它应该越来越像：

- ingress
- auth boundary
- route / forwarder

而不是业务逻辑中心。
