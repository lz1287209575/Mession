# Module: Router

`Source/Servers/Router` 是当前控制面服务。

## 当前职责

- 服务注册
- 服务发现
- 路由查询
- 玩家到 World 的稳定绑定

## 当前原则

Router 是控制面，不是高频业务代理。  
它不应承载：

- 客户端入口
- 高频移动流量
- World 业务状态
