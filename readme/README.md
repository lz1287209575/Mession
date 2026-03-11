# 文档总览

这个目录用于存放按模块拆分后的项目文档，避免根 `README.md` 变成单一大文件。

## 阅读顺序

如果你第一次看这个项目，建议按下面顺序阅读：

1. `../README.md`
2. `protocol.md`
3. `gateway.md`
4. `login.md`
5. `world.md`
6. `scene.md`
7. `router.md`

## 基础层

- [`core.md`](./core.md): 基础类型、socket、完整包收发
- [`common.md`](./common.md): 日志和后端长连接抽象
- [`netdriver.md`](./netdriver.md): 网络对象与复制系统

## 服务模块

- [`gateway.md`](./gateway.md): 客户端入口、消息分发、后端路由
- [`login.md`](./login.md): 登录结果生成与 `SessionKey` 校验
- [`world.md`](./world.md): 玩家主状态、世界逻辑、复制与场景同步
- [`scene.md`](./scene.md): 场景实体视图与世界服协作
- [`router.md`](./router.md): 控制面注册、服务发现、玩家世界服绑定

## 协议与架构

- [`protocol.md`](./protocol.md): 客户端协议、跨服协议和 Router 控制面消息
- [`../README.md`](../README.md): 项目总览、架构图、快速开始

## 当前文档划分原则

- 根 `README.md` 负责项目总览和快速入口
- `readme/` 目录负责分模块说明
- Router 的设计与实现说明统一收敛到 `router.md`
