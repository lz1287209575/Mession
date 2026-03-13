# Core 模块（`Source/Core`）

`Source/Core` 提供项目最底层的通用能力，是所有上层模块共同依赖的基础层。  
在新的目录结构下，它被拆分为四个子模块：`Event/`、`Concurrency/`、`Containers/`、`Net/`。

## 目录结构

```text
Source/Core/
  Event/        # 事件循环
  Concurrency/  # 任务与并发
  Containers/   # 自定义容器
  Net/          # 网络基础
```

## Event：事件循环

位置：`Source/Core/Event/`

- `EventLoop.h/.cpp`：单线程网络事件循环 `MNetEventLoop`
  - 维护监听 socket 列表和连接表
  - 使用 `poll` 监听读事件，接收完整包并回调
- `MEventLoop.h/.cpp`：主事件循环 `MEventLoop`
  - 管理一组 `IEventLoopStep` 子循环
  - `RunOnce()` 依次调用每个子循环的 `RunOnce(timeoutMs)`
- `TaskEventLoop.h/.cpp`：任务事件循环 `MTaskEventLoop`
  - 同时实现 `ITaskRunner` 和 `IEventLoopStep`
  - `RunOnce()` 只负责执行已投递的任务
- `IEventLoopStep.h`：事件循环步骤接口

这部分不关心具体业务，只负责**每帧如何轮询 IO 和任务**。

## Concurrency：任务与并发

位置：`Source/Core/Concurrency/`

- `ThreadPool.h/.cpp`：线程池 `MThreadPool`
  - 内部基于 `MTaskQueue` 抽象任务队列
  - 负责工作线程的生命周期管理
- `TaskQueue.h/.cpp`：多生产者多消费者任务队列 `MTaskQueue`
- `Async.h` / `Promise.h`：轻量异步封装（类似简化版 `std::future`/`std::promise`）
- `ITaskRunner.h`：任务投递接口，供上层统一使用

这一层为事件循环、服务器基类、脚本等提供**统一的任务调度能力**。

## Containers：自定义容器

位置：`Source/Core/Containers/`

- `RingBuffer.h`：环形缓冲区
  - 用于网络收发、日志等对性能敏感的场景

目标是逐步把项目内常用的基础容器抽象出来，减少直接散落使用原始 STL 的场景。

## Net：网络基础

位置：`Source/Core/Net/`

- `NetCore.h/.cpp`：基础类型别名、常量、时间/日志辅助等核心设施
- `SocketPlatform.h/.cpp`：平台 socket API 适配（WinSock / POSIX）
- `SocketAddress.h` / `SocketHandle.h`：地址值对象和 socket 句柄 RAII 封装
- `Socket.h/.cpp`：`MTcpConnection` 和 `MSocket` 辅助
  - 统一 TCP 连接与完整包收发逻辑
  - 处理半包、粘包、多包发送与接收缓冲
- `PacketCodec.h`：长度前缀包编解码
  - 实现 `Length(4) + MsgType(1) + Payload(N)` 的收发协议
- `Poll.h`：`poll` 的轻量封装

这一层只关心**socket 的生命周期与完整二进制包**，不关心具体消息语义。

## 设计边界

`Source/Core` 不处理业务语义：

- 不关心玩家是谁、在哪个场景
- 不关心消息类型具体含义
- 不关心服务器拓扑结构

它只负责：

- 基础类型和工具（`NetCore`）
- 网络 IO 和完整包收发（`Net`）
- 事件循环与任务调度（`Event`、`Concurrency`）
- 性能敏感的通用容器（`Containers`）

## 与其他模块的关系

- `Source/Common/ServerConnection` 基于 `Core/Net/Socket` 和 `Core/Net/Poll`
- `Gateway / Login / World / Scene / Router` 都依赖 `Source/Core`
- `Source/NetDriver` 的复制消息依赖 `MTcpConnection` 和统一包格式

推荐从下往上理解：**先看本文件，再看 `common.md` / `netdriver.md` / 各服务模块文档**。
