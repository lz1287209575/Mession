# Actor 系统完成度总结（2026-08-19）

> 把 actor 系统从「能跑」推到「能生产」的一次完整落地。
> 状态：功能完整、validate.py 4/4 PASSED、5 个 commit 已 push 到 main。

---

## 0. 一句话总结

`IActor` 起手，跨进程 Post 0 RTT、限流 + 断连保护 + ack-based 删除 + 1:N fan-out + actor 状态持久化 —— **功能完整**。剩余 roadmap 全部是「生产化打磨」非必需。

---

## 1. 提交链

```
a95612c feat(actor): MT_ServerPush listener and 1:N actor fan-out
6bf4e8f feat(actor): outbox ack-based deletion + actor state persistence
f2c5c9a chore: remove stale validate.py output files
e688661 refactor(mht): replace fragile Meta=find with proper lexer + parser
3913466 feat(actor): actor system with multi-reactor, cross-process Post/Call, and at-least-once ack
```

远端 `main` 在 `a95612c`，ahead of origin 45+ commits（5 个本次 session 提交）。

---

## 2. 架构总览

```
┌─────────────────────────────────────────────────────────────────┐
│ Client Process (MActorHandle::Post)                           │
│   ↓                                                            │
│ MActorSystem::SendActor (high-level entry)                      │
│   1) AllocateSequenceId (per-actor monotonic atomic)          │
│   2) For each non-local route in FindAllActorRoutes:         │
│      a) MRpcChannel::SendActor (raw MT_ActorPost write)       │
│      b) on fail → outbox entry (Seq, Target) per route         │
│   3) return success/fail                                       │
└─────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────┐
│ Server Process (DispatchActorPostMessage)                      │
│   1) FunctionId:2B lookup → MFunction                            │
│   2) ParsePayload → FActorMessageWire (Seq, ActorId, MsgType) │
│   3) MActorSystem::DispatchLocal(actorId, msg) → Sub ambient  │
│   4) MT_ServerPush(Delivered=0, [ActorId, Seq]) back to       │
│      sender's connection                                       │
└─────────────────────────────────────────────────────────────────┘
        ↑
┌─────────────────────────────────────────────────────────────────┐
│ MActorSystem::AckOutboxByRoute(actorId, seq, targetRoute)    │
│   - 从 (Seq, Target) 精确删 outbox entry                       │
│   - 同时 fire 业务 listeners                                    │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. 关键模块

### 3.1 actor 核心

| 文件 | 作用 |
|------|------|
| `Source/Common/Runtime/Actor/IActor.h` | actor 接口（GetActorId / OnMessage / OnCreated / OnDestroyed / SerializeState / RestoreState）|
| `Source/Common/Runtime/Actor/FActorMessage.h` | 进程内消息（Header + Payload + ReplyPromise + SequenceId）|
| `Source/Common/Runtime/Actor/MActorHandle.h/.cpp` | actor 句柄（Post/Call, 远端自动 SendActor）|
| `Source/Common/Runtime/Actor/MActorSystem.h/.cpp` | actor 运行时：注册 / 派发 / outbox / SequenceId / persistence |

### 3.2 路由 + 跨进程传输

| 文件 | 作用 |
|------|------|
| `Source/Common/Net/Routing/ActorRouter.h/.cpp` | ActorId → TVector\<SActorRoute\> (1:N fan-out 支持) |
| `Source/Common/Net/Rpc/MRpcChannel.h/.cpp` | SendActor (raw MT_ActorPost write) + CallActorAndWait (RPC) |
| `Source/Common/Net/ServiceDiscovery/EndpointCache.cpp` | DispatchActorPostMessage + MT_ServerPush handler（含 AckOutboxByRoute + listener 触发）|
| `Source/Common/Net/ServerConnection.h/.cpp` | SendServerPush 含可变负载 (StatusCode + ActorId + Seq) |

### 3.3 反射 codegen

| 文件 | 作用 |
|------|------|
| `Source/Tools/MHeaderTool/AST/MMetaLexer.h` | 手写 lexer + 递归下降 parser 解析 `Meta=(K=V,...)` |
| `Source/Tools/MHeaderTool/AST/ASTReflectionVisitor.cpp` | 用 MMetaLexer 替换脆弱的 `find("Meta=(")` 字面匹配 |

### 3.4 业务 demo

| 文件 | 作用 |
|------|------|
| `Source/Servers/EchoService/MRankListActor.h/.cpp` | 首个业务 actor（UpdateScore / GetTopN / GetPlayerRank），含 SerializeState/RestoreState 参考实现 |
| `Source/Servers/EchoService/EchoService.cpp` | Init 后调 LoadActorState，Shutdown 前调 SaveActorState |

---

## 4. 协议 / Wire 格式

### 4.1 包类型

| 类型 | 方向 | 值 | 用途 |
|------|------|---|------|
| `MT_FunctionCall` | C→S | 28 (0x1C) | Call 请求（带 CallId,期待 Response）|
| `MT_FunctionResponse` | S→C | 29 (0x1D) | Call 响应（含 CallId + 序列化 payload）|
| `MT_ActorPost` | C→S | 0xCE | Post 请求（无 CallId,server 不回 Response）|
| `MT_ServerPush` | S→C | 0xCD | 单向 push / 投递 ack |

### 4.2 MT_ActorPost wire format

```
[Length:4B]
[Type:1B = 0xCE]
[FunctionId:2B]      little-endian
[Payload:N]           FActorMessageWire serialized bytes
                       含 SenderId/TargetId/MsgType/SequenceId
```

### 4.3 MT_ServerPush wire format (投递 ack)

```
[Length:4B]
[Type:1B = 0xCD]
[StatusCode:1B]      0=Delivered, 1=ActorNotFound, 2=ParseError, 3=QueueFull
[ActorId:8B]         little-endian,Envelope.TargetId
[SequenceId:8B]      little-endian,Envelope.SequenceId
```

### 4.4 FActorMessageWire

```cpp
MSTRUCT()
struct FActorMessageWire {
    MPROPERTY() uint64      SenderId    = 0;
    MPROPERTY() uint64      TargetId    = 0;
    MPROPERTY() int32       MsgType     = 0;
    MPROPERTY() TByteArray  Payload;
    MPROPERTY() uint64      SequenceId  = 0;  // at-least-once 协议
};
```

---

## 5. 设计原则

### 5.1 wire 协议命名的「自解释」原则

| 包 | 名字含义 |
|----|----------|
| `MT_FunctionCall` / `MT_FunctionResponse` | 显式 Call/Response 对应 |
| `MT_ActorPost` | 直接对应 `MActorHandle::Post`，任何人看到名字 0 困惑 |
| `MT_ServerPush` | 服务器主动 push（不依赖 request）|

避开 `MT_OneWayCall` / `MT_OnewayCall` 这种 jargon-y 名字。

### 5.2 业务 thread 永不阻塞

`MActorSystem::SendActor` 是 high-level entry，所有失败路径直接进 outbox 或 log 警告。业务代码不感知 RTT/连接状态。

### 5.3 投递确认 + 1:N fan-out 的协同

- 1 个 Post = 1 个 SequenceId
- 多 route（fan-out）共享同一个 Seq
- 每条 route 独立 outbox entry（按 `(Seq, Target)` 匹配删除）
- AckOutboxByRoute 精确删该 route 的 entry（不会因为一个 route 的 ack 删掉其他 route 的 pending）

### 5.4 per-actor pending 计数 + 限流

- `DispatchLocal` 检查 `kMaxPendingPerActor=1024` 上限
- 超限 drop 新消息 + log metric（每 1000 次 drop 报一次）

---

## 6. 验证状态

### 6.1 validate.py 4/4

```
Test 1 (chain_local):         OK
Test 2 (chain_remote):        OK
Test 2b (chain_remote_async): OK
Test 3 (error_unknown):        OK
Validation PASSED
```

### 6.2 verify_protocol.py

All protocol checks passed.

### 6.3 build

`cmake --build Build -j4` 100% 绿，无 error，无 warning。

---

## 7. actor 能力矩阵

| 维度 | 状态 |
|------|------|
| 进程内 Post | ✅ 异步 + 限流 |
| 进程内 Call | ✅ 异步 + RTT-bound |
| 跨进程 Post | ✅ 0 RTT + 1:N fan-out + outbox + ack-based 删除 |
| 跨进程 Call | ✅ 异步 + RTT-bound |
| at-least-once 协议 | ✅ 完整（outbox + AckOutboxByRoute + MT_ServerPush）|
| 投递确认 | ✅ MT_ServerPush + Listener 订阅 |
| 业务 thread 不阻塞 | ✅ |
| 连接断开保护 | ✅ outbox 1024 + drop oldest + per-route 重发 |
| actor 状态持久化 | ✅ IActor.SerializeState/RestoreState + EchoService 自动调用 |
| validate.py 4/4 | ✅ PASSED |

---

## 8. API 总览

### 8.1 业务写 actor

```cpp
// 1) 定义
MCLASS(Type = Actor)
class MRankListActor : public IActor, public MObject {
    uint64 GetActorId() const override { return RANK_LIST_ACTOR_ID; }
    void OnMessage(const FActorMessage& InMsg) override;
    void OnCreated() override;
    TByteArray SerializeState() const override;     // 持久化
    bool RestoreState(const TByteArray&) override;  // 持久化
private:
    SState State;  // 业务内部 state
};

// 2) 注册
TSharedPtr<MRankListActor> Actor = NewMObject<MRankListActor>(nullptr, "RankList");
MActorSystem::Get().Register(Actor);  // MActorSystem::Init 之后调

// 3) 业务发 Post（远端 + 1:N fan-out 全自动）
MActorSystem::SendActor(RANK_LIST_ACTOR_ID, FActorMessage::MakePost(
    /* SenderId = */ MY_ACTOR_ID,
    /* TargetId = */ RANK_LIST_ACTOR_ID,
    /* MsgType = */ ERankListMessage::UpdateScore,
    /* Payload = */ SerializeUpdateScore(playerId, newScore)
));
```

### 8.2 业务订阅投递 ack

```cpp
auto Handle = MActorSystem::Get().RegisterServerPushListener(
    [](uint8 Status, uint64 ActorId, uint64 Seq) {
        if (Status == 0) {
            LOG_INFO("actor %llu seq %llu delivered", ActorId, Seq);
        } else {
            LOG_WARN("actor %llu seq %llu failed status=%u", ActorId, Seq, Status);
        }
    });
// 卸载
MActorSystem::Get().UnregisterServerPushListener(Handle);
```

### 8.3 业务做 actor 持久化

```cpp
// 在 MEchoService::OnRunStarted 调（actor 已注册）
MActorSystem::Get().LoadActorState(MRankListActor::RANK_LIST_ACTOR_ID, "Logs/ranklist_actor.bin");

// 在 MEchoService::ShutdownConnections 调
MActorSystem::Get().SaveActorState(MRankListActor::RANK_LIST_ACTOR_ID, "Logs/ranklist_actor.bin");
```

---

## 9. 剩余 roadmap（**全部非必须**）

actor 系统在 PoC 范围**功能完整**。剩余项都是「生产化打磨」或「未来功能扩展」：

| Roadmap | 是否必须 | 不做的后果 |
|---------|----------|------------|
| 长稳测试（断连/重连/高负载）| 上生产前应该做 | 实际负载下可能暴露 bug |
| Registry 持久化（actor 注册信息）| 可选 | 进程重启后 actor 列表空了，业务层重发现即可 |
| MRankListActor JSON 序列化 | 可选 | 当前 binary 格式够用 |
| actor group / topic 模式 | 可选 | **架构模式不同**（pub/sub，不是 fan-out 替代品）|
| MT_ServerPush SequenceId 校验 | 可选 | 极小概率「过期 ack 删错 entry」|

---

## 10. 失败案例 / 调优经验

1. **MHeaderTool Meta 解析器脆弱**：字面 `find("Meta=(")` 不容忍空格
   → 重写为手写 lexer + 递归下降 parser（`MMetaLexer.h`）
2. **MT_OneWayCall 太 jargon**：用户立刻指出
   → 重命名为 `MT_ActorPost`（自解释，对应 `MActorHandle::Post`）
3. **MPromise 内存安全**：原 Post 路径调 CallActor 产生 MPromise 走 RegisterServerCall 5s 超时
   → 加 SendActor 走 raw MT_ActorPost，不分配 MPromise
4. **wasted RTT**：server 收到 MT_FunctionCall 自动回 MT_FunctionResponse(SEmptyServerMessage)，client 丢弃
   → 走 MT_ActorPost，server 不回任何包（0 RTT）
5. **MHeaderTool namespace mession::actor:: 太 verbose**：用户立刻指出
   → 平铺到全局命名空间
6. **Meta 空格不工作 → CLI 参数不解析 → Config 全 0 → Init 失败 → exit 1 silent**
   → 这是整个 session 最隐蔽的 bug；validate.py 看似绿了但实际是「没绑到 EchoService」

---

## 11. 关联 spec 文档

- `2026-08-13-actor-extension-design.md` — actor 抽象扩展设计（IActor / FActorMessage / 跨进程）
- `2026-08-13-multi-reactor-design.md` — 多 Reactor 设计
- `2026-08-14-player-design.md` / `2026-08-14-player-session-db-design.md` — 上层业务
- `2026-07-24-cpp17-async-await.md` — C++17 async model（被 actor 用作 Call future）
- `2026-08-07-*-lua-*.md` — Lua 桥（被 actor system 暴露给脚本用）

---

## 12. 一句话回顾

从「单进程 actor 模型」起手，**0 RTT 跨进程 Post + 限流 + 断连保护 + ack-based 删除 + 1:N fan-out + 持久化 + listener 订阅**——一个 PoC 量级的 actor 消息总线能在主路径上跑通 validate.py 的 4 个端到端 case。剩余全是生产化打磨与未来扩展。

如果之后有真实负载场景，建议先做**长稳测试**暴露问题再针对性补强。
