# Next Steps

这份文档只回答一个问题：  
如果现在继续推进 Mession，最值得做什么。

## 推荐优先级

### P1. 字段域与 dirty 域

这是当前最值得做的事情。

原因：

- Gameplay 方向已经明确
- replication 已经有 snapshot 主链路
- 真正缺的是“字段修改后该去哪里”的正式机制

目标：

- 给属性增加 `PersistentData`、`RepToClient` 等语义
- 实现按 domain 的 dirty tracking
- 让 persistence 和 replication 变成两个独立消费者

### P2. Persistence subsystem

在字段域之后，最自然的下一步就是最小 persistence 骨架。

目标：

- 把 DB 写回从业务对象里拆出来
- 形成“运行时对象 -> persistent export -> writeback”的标准流程

### P3. Gameplay member 继续拆分

在机制稳定后，再继续扩：

- Attribute
- Inventory
- Ability
- Interaction

原因：

- 没有字段域和持久化边界之前，先加业务类只会放大耦合

## 不推荐的优先项

下面这些事情不是不能做，而是现在做性价比低：

- AOI 大重构
- 全面替换底层网络模型
- 再加一批样例类型
- 为兼容历史路径继续扩 message-based glue
