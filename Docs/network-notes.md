# 🌐 Network Notes

这份文档收敛记录当前网络层相关的几个判断。

## Socket 层

第一阶段 socket 重构已经进入主链路：

- listener 与 accepted socket ownership 更清晰
- `MTcpConnection` 已统一主动连接与被动接入
- packet framing 已共享
- poll 重复逻辑已减少

当前结论是：socket 层先保持稳定，不是最优先主矛盾。

## 字节序

当前并没有完成全量统一网络字节序。  
项目仍处在逐步收敛阶段：

- 一部分跨服消息已按网络序处理
- 客户端脚本和现有联调仍依赖当前约定

协议字节序调整必须配套脚本验证。

## Asio / 替代网络模型

Asio 相关内容当前只属于调研储备，不是主线执行路线。

当前继续沿用自研模型的原因是：

- 主链路已稳定
- 现阶段瓶颈不在更换网络库
- 架构重点在协议、Gameplay、replication、persistence

只有在网络层成为明确瓶颈时，才值得重新把替代方案提到前面。
