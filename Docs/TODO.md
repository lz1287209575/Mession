# TODO

这份文档只记录当前仍然值得推进的事情。  
已经完成并稳定的内容，不再反复拆成历史任务单。

## 当前判断

主链路已经稳定：

- Router / Gateway / Login / World / Scene 五服可联通
- 客户端正式入口已经收口到 `MT_FunctionCall`
- UE 下行正式路径已经收口到统一函数调用
- 反射驱动的 Actor snapshot / replication 已可用
- `Scripts/validate.py` 已成为默认回归入口

现在的重点不是继续补临时 glue，而是把业务骨架做对。

## Now

### 0. 分布式持久化两条铁律（必须先落地）

目标：避免多服并发写库导致双写冲突，并保证存储层故障可恢复。

核心原则：

- 单写主原则：同一份玩家数据在同一时刻只能有一个写主（owner）服务器
- 幂等重试原则：所有持久化请求必须带唯一请求标识与版本语义，可安全重放

建议优先落地：

- World -> MgoServer 持久化通道只接受 owner 写请求
- 持久化请求带 `request_id` 与 `version`（或 fencing token）
- Mongo upsert 使用条件更新（只接受更新版本）
- Mgo 不可用时，World 本地队列/日志缓存并重试重放

完成标准：

- 同一玩家跨服切换时不会出现双写覆盖
- MgoServer 短时不可用不会导致持久化数据丢失
- 重试发送不会产生重复副作用

### 1. 字段域与 dirty 域

目标：让属性声明天然表达“同步给谁”和“是否持久化”。

建议优先落地：

- `MPROPERTY(PersistentData, RepToClient)` 这类字段域语义
- 反射属性元数据增加 domain flags
- 运行时按 domain 记录 dirty 状态
- replication 消费 `Client` dirty 域
- persistence 消费 `Persistent` dirty 域

完成标准：

- 业务字段修改后，不需要手写“顺便写库”“顺便发客户端”
- DB 和客户端复制各自按自己的节奏消费 dirty 集合

### 2. Gameplay 骨架继续定型

目标：让 `Gameplay` 真正成为领域模型层，而不是 World 的杂项容器。

建议优先落地：

- 明确 `MPlayerAvatar` 的公共运行时字段
- 明确 `AvatarMember` 的能力拆分边界
- 把更多规则放进 member，而不是继续堆在 `WorldServer`

完成标准：

- 新增一类玩法能力时，优先知道应该落在哪个 member
- `WorldServer` 更像流程编排，而不是巨型业务对象

### 3. Persistence 骨架

目标：补出“运行时对象”和“持久化输出”之间的正式边界。

建议优先落地：

- persistence subsystem 的最小接口
- 从反射元数据导出 persistent 字段
- 按批次或时机刷盘，而不是属性改动即写库

完成标准：

- `Avatar` 不直接等于 DB schema
- 持久化流程不再散落在业务代码里

## Next

### 4. Client / UE 协议继续收口

继续完善：

- 统一函数调用负向验证
- 未知 `FunctionID`
- payload decode 失败
- 未鉴权调用
- route pending / route unavailable

### 5. 生成链路补强

继续完善：

- `MHeaderTool` 对更多声明形式的覆盖
- unsupported 场景的显式报错
- 生成日志与运行时日志的边界

### 6. 复制系统元数据化

继续完善：

- 复制规则进一步挂回反射元数据
- 后续支持更细粒度的复制条件和通知

## 推荐优先级

如果现在只做一件事，优先建议：

1. 字段域与 dirty 域
2. persistence subsystem
3. Gameplay member 继续拆分

当前不建议抢优先级的事情：

- AOI 大重构
- 全面替换底层网络模型
- 再加一批样例类型
- 为兼容历史路径继续扩 message-based glue

## Later

这些事情有价值，但不该先做：

- AOI 大重构
- 更大规模的多区 / 多 World
- 更重的压力测试常态化
- 更深的底层网络模型替换

## 不再作为当前重点

下面这些方向目前不应抢占主线：

- 扩样例反射对象
- 增长历史兼容入口
- 把业务继续塞回 `GatewayServer` 或 `WorldServer` 大分支
