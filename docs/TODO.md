# Mession Sprint Board

> 仓库统一 TODO，只维护这一份。  
> 当前目标：先把主链路补完整并稳定回归，再推进 AOI、协议和架构整理。

## Snapshot

- [x] CMake 构建通过
- [x] `scripts/validate.py` 登录 / 移动 / 重连验证通过
- [x] Router / Gateway / Login / World / Scene 最小链路已打通
- [x] Core 类型补齐、时间抽象、基础线程安全项已完成

当前缺口：
- [ ] 状态回收仍不够一致
- [ ] 分布式复制链路未真正补完
- [ ] 测试体系还没有形成正式入口
- [x] Socket 网络层第一阶段收口已完成

---

## Now

- [ ] 修复 `Gateway` 处理 `MT_PlayerLogout` 时的连接索引错误
- [ ] 统一客户端断线、World 清理、Gateway 鉴权状态回收的行为
- [ ] 检查 `World -> Gateway -> Client` 的登出 / 失效通知是否完整闭环
- [ ] 为上述清理路径补最小可复现验证
- [ ] 在 `WorldServer` 中为客户端连接补 `MReplicationDriver::AddConnection()`
- [ ] 玩家进入世界时补齐 `BroadcastActorCreate()`
- [ ] 玩家离开世界时补齐 `RemoveConnection()` / `ActorDestroy` / 相关可见性清理
- [ ] 验证 `ActorCreate / ActorUpdate / ActorDestroy` 在分布式模式下真实可达

## Next

- [ ] 整理 CMake / CI 测试入口，明确 `ctest` 是否承载真实测试
- [ ] 为 `ServerMessages` / `MessageUtils` / 协议组包解包补单元测试
- [ ] 把登录、进世界、断线清理整理成稳定的集成测试脚本
- [ ] 补一条“多玩家进入世界后复制链路正常”的验证
- [ ] 清理 `SceneServer` 和剩余服务上的网络循环样板，决定是否继续扩展 `MSocketPoller`

## Later

- [ ] 修正 `AOIComponent` 中 `TMap<SAOICell, SAOICell>` 的语义设计
- [ ] 明确 AOI 数据结构，改为“格子 -> 对象集合”
- [ ] 将 AOI 接入 `WorldServer` 的可见性计算
- [ ] 让 AOI 结果驱动 `ReplicationDriver::RelevantActors`
- [ ] 为跨格移动、进出视野、离线清理补验证
- [ ] 将 `ParsePayload` 从纯 `bool` 结果逐步提升为带错误信息的返回值
- [ ] 统一协议解析失败时的日志格式和上下文信息
- [ ] 继续收敛跨服消息结构体，减少裸 payload 处理
- [ ] 评估并逐步统一字节序策略，避免跨平台协议隐患
- [ ] 评估 `NetDriver/Reflection.h` 是正式接入主线，还是降级为示例 / 实验代码
- [ ] 若继续保留 `Reflection`，明确它和复制系统、运行时对象系统的边界

## Watchlist

- [ ] `MServerConnectionManager` 是否还有重新接入的必要，现阶段先保持文档说明即可
- [ ] 更系统的 `TResult` / 错误码体系改造
- [ ] 继续收敛散落的设计说明，避免新的文档分叉

## Done

- [x] Core 常用 STL 类型补齐
- [x] `MUniqueIdGenerator` 线程安全
- [x] `MLogger` 基础线程安全与文件输出
- [x] `MTime` 统一时间抽象
- [x] `INetConnection` 抽象统一
- [x] `MServerConnectionManager` 未接入原因已文档化
- [x] `TResult<T, E>` 基础类型已提供
- [x] 共享指针构造统一为项目级 `MakeShared`
- [x] Socket 网络层第一阶段收口：`SocketPlatform` / `SocketAddress` / `SocketHandle` / `PacketCodec` / `MTcpConnection` / `MServerConnection` / `MSocketPoller`

---

*最后更新：基于仓库扫描、构建结果和 `scripts/validate.py` 验证结果整理*
