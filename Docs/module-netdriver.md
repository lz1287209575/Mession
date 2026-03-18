# Module: NetDriver

`Source/NetDriver` 负责反射、snapshot 和 replication。

## 当前职责

- 提供反射运行时
- 提供 snapshot 读写能力
- 提供 Actor create / update / destroy 的复制驱动

## 当前结论

这里已经不再应围绕历史样例对象设计。  
主线应该是：

- 反射作为元数据基础
- snapshot 作为对象状态传输形式
- replication 作为客户端持续状态同步机制

## 当前边界

它不负责：

- 登录流程
- 路由
- World 业务编排
- DB 写回调度
