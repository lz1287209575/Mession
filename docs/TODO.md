# Mession Sprint Board

> 仓库统一 TODO，只维护这一份。  
> 当前目标：先把主链路补完整并稳定回归，再推进 AOI、协议和架构整理。

## Snapshot

- [x] CMake 构建通过
- [x] `scripts/validate.py` 登录 / 移动 / 重连验证通过
- [x] Router / Gateway / Login / World / Scene 最小链路已打通
- [x] Core 类型补齐、时间抽象、基础线程安全项已完成

当前缺口：
- [x] 测试体系：脚本验证入口与前置条件已文档化（README + docs/validation.md），CI 已跑协议脚本与主链路验证
- [x] Socket 网络层第一阶段收口已完成
- [x] 分布式复制链路（World 隧道 + AddConnection/BroadcastActorCreate/RemoveConnection）已补齐
- [x] Gateway 登出与鉴权状态回收已与 World 闭环

---

## Now

- [ ] （无；当前项已收口）

### 验证方案（脚本，不引入 ctest）

- **清理路径**：多玩家登录后，关闭其中一人的连接；等待后由另一玩家发移动；断线玩家重连并用同一 `PlayerId` 登录。已接入 `scripts/validate.py`（Test 3），失败则脚本不通过。
- **复制链路**：玩家登录后，在约定时间内从客户端连接收包，断言至少收到一条 `MT_ActorCreate`。已接入 `scripts/validate.py`（Test 2）为**硬断言**。根因与修复：World 每 Tick 未刷新后端连接发送缓冲，复制包在 EAGAIN 时滞留；已改为每帧对 `BackendConnections` 调用 `FlushSendBuffer()`。跑验证前需保证端口 8001–8005 未被旧进程占用。

## Next

- [x] 整理 CI 下的脚本测试入口与文档（不引入 ctest）
- [x] 为 `ServerMessages` / 协议组包解包补可脚本化或独立可执行的小验证（`scripts/verify_protocol.py`，CI 全矩阵执行）
- [x] 把登录、进世界、断线清理整理成稳定的集成测试脚本并纳入 CI（`scripts/validate.py` Test 1/2/3，CI 中 Linux GCC 执行）
- [x] 清理 `SceneServer` 和剩余服务上的网络循环样板，决定是否继续扩展 `MSocketPoller`（决策：保持 MSocketPoller 薄封装，各服保留独立 accept+poll+处理 循环，见 `docs/socket-layer-refactor.md`）

## Later

- [ ] 修正 `AOIComponent` 中 `TMap<SAOICell, SAOICell>` 的语义设计
- [ ] 明确 AOI 数据结构，改为“格子 -> 对象集合”
- [ ] 将 AOI 接入 `WorldServer` 的可见性计算
- [ ] 让 AOI 结果驱动 `ReplicationDriver::RelevantActors`
- [ ] 为跨格移动、进出视野、离线清理补验证
- [x] 将 `ParsePayload` 从纯 `bool` 结果提升为 `TResult<void, FString>`，失败时返回描述信息（payload_size / deserialize_failed | read_overflow | trailing_bytes）
- [x] 统一协议解析失败时的日志格式：调用处统一为 `LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str())`，可选 Context 参数写入错误串前缀
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
- [x] Gateway 处理 `MT_PlayerLogout` 时按 `PlayerId` 查找连接并重置鉴权状态
- [x] World → Gateway 登出通知与 Gateway 鉴权状态回收闭环
- [x] WorldServer 为每个玩家建立复制隧道、`AddConnection` / `BroadcastActorCreate` / `RemoveConnection`，按 `PlayerId` 的 `MT_PlayerClientSync` 回程
- [x] 会话校验请求改用 World 内全局 `ValidationRequestId`，避免多网关连接号冲突
- [x] Gateway ↔ World 玩家数据路由统一为 `PlayerId`（`MT_PlayerClientSync` + `SPlayerClientSyncMessage`）
- [x] World 每 Tick 刷新后端连接发送缓冲，复制包可稳定到达 Gateway → Client
- [x] `scripts/validate.py` Test 2 复制链路改为硬断言；Test 3 清理路径已硬断言
- [x] CI：脚本验证入口与前置条件文档化（README + docs/validation.md）；协议小验证 `scripts/verify_protocol.py` 全矩阵执行；主链路验证 Linux GCC 执行
- [x] 网络循环样板与 MSocketPoller 决策：保持薄封装、各服独立循环，见 `docs/socket-layer-refactor.md`
- [x] 基础库补齐：Config 增加 GetInt/GetU32/GetU64/GetBool；Core 增加 MNonCopyable、TSpan/TSpanMutable（C++20）；World/Login 配置项从 Config 读取 max_players、server_name、session_key_min/max
- [x] ParsePayload 返回 `TResult<void, FString>`，可选 Context 参数；所有调用处统一日志格式 "ParsePayload failed: %s"

---

*最后更新：基于仓库扫描、构建结果和 `scripts/validate.py` 验证结果整理*
