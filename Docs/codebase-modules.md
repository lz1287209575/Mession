# 🧩 Codebase Modules

这份文档集中说明 Mession 主要源码目录的职责。

## `Source/Core`

负责最底层运行基础：

- 事件循环
- 任务与并发
- 网络基础设施
- 通用底层类型与容器

它不关心业务语义，只负责“让程序能跑、能收发、能调度”。

## `Source/Common`

负责跨模块复用的公共组件：

- 日志
- 配置
- 字符串与消息工具
- 服务间长连接抽象
- 跨服消息结构

它是公共支撑层，不是业务层。

## `Source/NetDriver`

负责反射、snapshot 和 replication：

- 反射运行时
- snapshot 读写
- Actor create / update / destroy 复制驱动

它不负责登录、路由或 World 业务编排。

## `Source/Gameplay`

负责领域模型：

- `MPlayerAvatar`
- `AvatarMember`
- Attribute / Movement / Interaction 等能力拆分

它不应承载登录、路由、DB 调度这类流程逻辑。

## `Source/Servers/Gateway`

客户端唯一正式入口。

负责：

- 客户端接入
- 协议解码
- 鉴权前后状态管理
- 路由查询
- 向 Login / World 转发

不负责 World 业务规则。

## `Source/Servers/Login`

负责会话与认证：

- 登录请求
- `SessionKey`
- 在线会话
- 为 World 提供会话校验

不负责 World 运行时状态。

## `Source/Servers/World`

当前主要运行时业务服。

负责：

- 接收 Gateway 转发的已鉴权请求
- 创建与管理玩家 Avatar
- 驱动 Gameplay tick
- 驱动 replication
- 协同 Scene

它应逐步把业务规则下沉到 `Source/Gameplay/`。

## `Source/Servers/Scene`

World 的下游视图协作节点。

负责：

- 接收世界状态同步
- 维护场景镜像实体
- 作为后续 AOI / 可见性扩展的落点

当前不是最优先扩展的模块。

## `Source/Servers/Router`

当前控制面服务。

负责：

- 服务注册
- 服务发现
- 路由查询
- 玩家到 World 的稳定绑定

它是控制面，不是高频业务代理。

## `Source/Messages`

负责协议层枚举和边界定义。

当前协议大致分成两层：

- `Client <-> Gateway`
- `Server <-> Server`

客户端正式入口已经收口到统一函数调用；跨服仍使用项目内部消息与 RPC 机制。
