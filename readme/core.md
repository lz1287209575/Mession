# Core 模块

## 作用

`Core/` 提供项目最底层的通用能力，主要包括：

- `NetCore.h`: 基础类型别名、常量、容器封装
- `Socket.h` / `Socket.cpp`: 底层 TCP socket 和 `MTcpConnection` 包收发实现

这是整个项目所有上层模块共同依赖的基础层。

## 主要职责

### `NetCore`

负责统一项目中常用的基础类型和 STL 包装类型，例如：

- `TString`
- `TArray`
- `TMap`
- `TSharedPtr`
- `TFunction`

这层的目标是让项目代码保持统一风格，避免在业务层直接散落使用原始 STL 名称。

### `Socket / MTcpConnection`

负责统一 TCP 连接与完整包收发逻辑：

- 监听 socket 创建
- 非阻塞 socket 设置
- 接收缓冲和发送缓冲
- 半包、粘包、多包处理
- 统一包格式 `Length(4) + Payload(N)`

`MTcpConnection` 是服务端和客户端链路中最常用的连接抽象。

## 设计边界

`Core/` 不处理业务语义：

- 不关心玩家是谁
- 不关心消息类型代表什么
- 不关心服务器之间的角色关系

它只负责“把完整的字节包稳定收发出来”。

## 与其他模块的关系

- `Common/ServerConnection` 基于 `Core/Socket`
- `Gateway / Login / World / Scene / Router` 都依赖 `Core/`
- `NetDriver` 的复制消息也依赖 `MTcpConnection` 下发
