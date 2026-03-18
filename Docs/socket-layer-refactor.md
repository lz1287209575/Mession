# Socket Layer Refactor

这份文档记录 socket 层重构方向。

## 当前状态

第一阶段已经完成并进入当前主链路：

- listener 与 accepted socket 的 ownership 更清晰
- `MTcpConnection` 已统一主动连接与被动接入路径
- packet framing 已共享
- poll 相关重复逻辑已减少

## 当前结论

socket 层现在不是当前项目最优先的主矛盾。  
它应保持稳定，除非出现明确瓶颈或协议演进需求。

## 什么时候继续推进

只有在下面场景中，才值得继续深入：

- 网络层成为明显瓶颈
- 连接生命周期管理再次暴露出结构问题
- 协议层改动需要 socket 层配合
