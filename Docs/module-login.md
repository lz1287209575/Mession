# Module: Login

`Source/Servers/Login` 负责会话与认证相关工作。

## 当前职责

- 处理登录请求
- 生成 `SessionKey`
- 管理在线会话
- 为 World 提供会话校验

## 当前边界

它不负责：

- World 中的玩家运行时状态
- 客户端连接管理
- Router 的选路逻辑
