# 干掉 AdoptConnection 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** 干掉 `MServerConnection::AdoptConnection` 后期接管 Conn 的接口，改为构造期接管；同时让 EchoService 在 OnAccept 里 `std::move(Conn)` 给 EventLoop，从根上消除"两条 shared_ptr alias 同一 MTcpConnection"导致的双重释放。

**Architecture:**
- `MServerConnection` 新增一个接收 `(SServerConnectionConfig, TSharedPtr<INetConnection>)` 的构造函数，把原本 `AdoptConnection` 的逻辑（包 MTcpMessageChannel + State=Authenticated）折叠进来。
- `AdoptConnection` 接口整段移除（含 double-free 警告注释）。
- EchoService 的 OnAccept 同步改为 `MakeShared<MServerConnection>(IncomingConfig, Conn)`，并把 `EventLoop.RegisterConnection` 的第二个参数改为 `std::move(Conn)`。
- `MTcpMessageChannel` 类本身保留——`TryConnect` 出站路径还在用。
- `MMessageChannel` 抽象、`MNetEventLoop::RegisterConnection` 签名、`MNetServerBase::OnAccept` 接口全部不动。

**Tech Stack:** C++20, TSharedPtr (std::shared_ptr 包装), MNetServerBase, MNetEventLoop, MServerConnection, MTcpMessageChannel, INetConnection/MTcpConnection

## Global Constraints

- 所有代码块都是可直接粘贴的最终代码
- 路径以 `/root/Mession/` 为根
- diff 格式用 `- 旧 / + 新`
- 命名规则保持项目现有约定：`M*` 类、`S*` 结构体、`b*` bool、`T*` 模板别名（`TSharedPtr` 等）
- 缩进 4 空格，不改文件原有风格
- 包含 `Common/Runtime/Log/Logger.h` 的 LOG_INFO/LOG_WARN 风格照旧
- 不要新增任何编译开关（无 `#ifdef`）
- 不引入新的第三方依赖
- 每次 commit 单独，不要把多 task 合一
- **不**动 `MTcpMessageChannel` 类本身（TryConnect 还在用）

---

## Task 1: 移除 AdoptConnection 接口（头文件 + 实现）

**Files:**
- Modify: `Source/Common/Net/ServerConnection.h:194-200`（删除 AdoptConnection 声明 + 上方 4 行注释）
- Modify: `Source/Common/Net/ServerConnection.cpp:31-43`（删除 AdoptConnection 实现 + 5 行注释）

**Context:**
- `MServerConnection::AdoptConnection` 是后期接管 Conn 的入口。原本设计意图是把 accept 进来的 `INetConnection` 包成 `MTcpMessageChannel` 塞进 `Transport` 成员。
- 这个接口是 EchoService.cpp:74 当前唯一调用方。
- 注释（cpp:38-43）警告了"两条 shared_ptr alias 同一 MTcpConnection* 会 double-free"——这正是当前 coredump 的根因。
- 干掉它之后接管逻辑折叠到构造函数（Task 2）。

- [ ] **Step 1: 删除 ServerConnection.h 里的 AdoptConnection 声明**

把这段（:194-200 区域）整段删除：

```cpp
    /**
     * AdoptConnection — 给已 accept 的 INetConnection 包一层 transport，把 MServerConnection
     * 直接置为 Connected + Authenticated（跳过 handshake RPC）。ServerType 传 Unknown
     * 让 handshake 走兼容路径（不依赖具体 endpoint Class）。
     */
    void AdoptConnection(TSharedPtr<INetConnection> Conn);
```

最终 `Source/Common/Net/ServerConnection.h` 的 `public:` 区只剩 `Connect/Disconnect/IsConnected/IsConnecting/GetState/GetRemoteServerInfo/SendPacket/SendPacketRaw/Tick/SetConfig/GetConfig/SetOnConnect/SetOnDisconnect/SetOnMessage/SetOnAuthenticated/SetLocalInfo/GetLocalInfo` 这些公开方法。

- [ ] **Step 2: 删除 ServerConnection.cpp 里的 AdoptConnection 实现**

把 `void MServerConnection::AdoptConnection(TSharedPtr<INetConnection> Conn)` 整段（含上方 5 行注释）删除。当前 cpp:31-43 是这样：

```cpp
void MServerConnection::AdoptConnection(TSharedPtr<INetConnection> Conn)
{
    if (!Conn)
    {
        return;
    }

    // 让 MServerConnection 接管 Conn 的所有权——后续 dispatch / close 走 Transport 这条链。
    // OnAccept 不再让 EventLoop 同时保留这个连接（否则两条 shared_ptr alias 同一
    // MTcpConnection* 会 double-free）。
    Transport = MakeShared<MTcpMessageChannel>(std::static_pointer_cast<MTcpConnection>(Conn));
    State = EConnectionState::Authenticated;
}
```

整段删除。

- [ ] **Step 3: 编译验证**

```bash
cd /root/Mession
cmake --build Build -j4 2>&1 | tee /tmp/build-after-remove-adopt.log
```

预期：编译**失败**，错误指向 `Source/Servers/EchoService/EchoService.cpp:74` 引用了已删除的 `AdoptConnection`（"no member named 'AdoptConnection'"）。这是预期——证明删除生效了，Task 2、Task 3 修复后会再次变绿。

- [ ] **Step 4: Commit**

```bash
cd /root/Mession
git add Source/Common/Net/ServerConnection.h Source/Common/Net/ServerConnection.cpp
git commit -m "refactor: drop MServerConnection::AdoptConnection

接口合并到构造期，详见 Docs/superpowers/specs/2026-07-10-drop-adopt-connection-design.md"
```

---

## Task 2: 新增 MServerConnection 双参构造函数（接收 INetConnection）

**Files:**
- Modify: `Source/Common/Net/ServerConnection.h:151-156`（在单参构造函数后新增双参构造函数声明）
- Modify: `Source/Common/Net/ServerConnection.cpp`（在单参构造函数实现后新增双参构造函数实现）

**Context:**
- 替代原 `AdoptConnection` 的逻辑。新构造函数接 `TSharedPtr<INetConnection>` 入参，内部把 Conn 包成 `MTcpMessageChannel` 塞进 `Transport`，置 State=Authenticated。
- `MTcpMessageChannel` 头文件已经在 ServerConnection.h 顶部 include 过（line 99-114 已有定义）。
- 不需要改任何公共头依赖——`INetConnection` 和 `MTcpConnection` 都已经在本文件内可见。

- [ ] **Step 1: 在 ServerConnection.h 单参构造函数后插入双参构造函数声明**

定位 `Source/Common/Net/ServerConnection.h:152-155` 的：

```cpp
    explicit MServerConnection(const SServerConnectionConfig& InConfig) : Config(InConfig)
    {
        UpdateLogPrefix();
    }
```

在它**之后**插入：

```cpp
    /** 入站连接构造函数：把已 accept 的 INetConnection 包成 Transport，置 Authenticated。
     *  替代原 AdoptConnection，详见 Docs/superpowers/specs/2026-07-10-drop-adopt-connection-design.md。 */
    MServerConnection(
        const SServerConnectionConfig& InConfig,
        TSharedPtr<INetConnection> InConn);
```

新构造函数用初始化列表 + 函数体，函数体里再调 `UpdateLogPrefix()` 与字段初始化（与单参版保持对称）。

- [ ] **Step 2: 在 ServerConnection.cpp 单参构造函数实现后插入双参构造函数实现**

定位 `Source/Common/Net/ServerConnection.cpp` 的 `MServerConnection::MServerConnection(const SServerConnectionConfig&)` 之后（目前未单独定义 cpp 版，看一下文件；如果只有头文件 inline 实现，则在 cpp 顶部 `void MServerConnection::UpdateLogPrefix()` 之前插入）。

在 cpp 文件里、`void MServerConnection::UpdateLogPrefix()` 之前插入：

```cpp
MServerConnection::MServerConnection(
    const SServerConnectionConfig& InConfig,
    TSharedPtr<INetConnection> InConn)
    : Config(InConfig)
{
    UpdateLogPrefix();
    HeartbeatInterval = InConfig.HeartbeatInterval;
    ReconnectInterval = InConfig.ReconnectInterval;
    if (InConn)
    {
        // 包成 Transport 接管 Conn，置 Authenticated（跳过 handshake）。
        Transport = MakeShared<MTcpMessageChannel>(std::static_pointer_cast<MTcpConnection>(InConn));
        State = EConnectionState::Authenticated;
    }
}
```

- [ ] **Step 3: 编译验证（预期仍失败，等 Task 3 修复）**

```bash
cd /root/Mession
cmake --build Build -j4 2>&1 | tee /tmp/build-after-ctor.log
```

预期：编译**失败**，错误指向 `EchoService.cpp:74` 仍然调用了已删除的 `AdoptConnection`。这证明构造函数已就位、只差 EchoService 改写。

- [ ] **Step 4: Commit**

```bash
cd /root/Mession
git add Source/Common/Net/ServerConnection.h Source/Common/Net/ServerConnection.cpp
git commit -m "feat: MServerConnection 构造期接管 INetConnection

新增双参构造函数 MServerConnection(const SServerConnectionConfig&, TSharedPtr<INetConnection>)，
把原 AdoptConnection 的逻辑（包 MTcpMessageChannel + State=Authenticated）折叠进来。
EchoService 后续 Task 切换到新入口。"
```

---

## Task 3: EchoService 切到新构造函数 + std::move(Conn) 给 EventLoop

**Files:**
- Modify: `Source/Servers/EchoService/EchoService.cpp:73-74`（合并单行 MakeShared）
- Modify: `Source/Servers/EchoService/EchoService.cpp:101`（第二个参数改为 std::move(Conn)）

**Context:**
- 这是消除 double-free 的关键修复。
- 两处改动缺一不可：仅改 73-74 不改 101，则两条 shared_ptr 路径仍 alias 同一 MTcpConnection；仅改 101 不改 73-74，则 EchoService 没法让 MServerConnection 拿到 Conn 来 send/receive。
- OnRead lambda 当前只捕获 `PeerConn`（MServerConnection），不捕获 Conn，所以 `std::move(Conn)` 给 RegisterConnection 不影响 OnRead 工作（`(void)PeerConn;` 是占位）。
- OnMessage lambda 捕获的是 `Connection`（MServerConnection 由 OnMessage 签名提供），跟 Conn 也无关。

- [ ] **Step 1: 改 EchoService.cpp:73-74 为合并单行**

当前 `Source/Servers/EchoService/EchoService.cpp:73-74`：

```cpp
    TSharedPtr<MServerConnection> PeerConn = MakeShared<MServerConnection>(IncomingConfig);
    PeerConn->AdoptConnection(Conn);
```

替换为：

```cpp
    TSharedPtr<MServerConnection> PeerConn = MakeShared<MServerConnection>(IncomingConfig, Conn);
```

- [ ] **Step 2: 改 EchoService.cpp:101 第二个参数为 std::move(Conn)**

当前 `Source/Servers/EchoService/EchoService.cpp:101-104`：

```cpp
    EventLoop.RegisterConnection(
        ConnId,
        Conn,
        [PeerConn](uint64 /*ConnectionId*/, const TByteArray& /*Payload*/)
```

替换为：

```cpp
    EventLoop.RegisterConnection(
        ConnId,
        std::move(Conn),
        [PeerConn](uint64 /*ConnectionId*/, const TByteArray& /*Payload*/)
```

- [ ] **Step 3: 编译验证**

```bash
cd /root/Mession
cmake --build Build -j4 2>&1 | tee /tmp/build-after-echosvc.log
```

预期：编译**成功**。`grep -rn "AdoptConnection" /root/Mession/Source` 应零结果。

- [ ] **Step 4: 验证 grep 残留**

```bash
grep -rn "AdoptConnection" /root/Mession/Source 2>/dev/null
```

预期：零输出。如果仍有残留（如注释、字符串字面量），手工检查——只有当残留**确实是死的代码引用**才算失败；文档里出现的 "AdoptConnection" 字面不阻断，但 spec 文档例外（已不再提及）。

- [ ] **Step 5: Commit**

```bash
cd /root/Mession
git add Source/Servers/EchoService/EchoService.cpp
git commit -m "fix(echo): use ctor-time adopt + std::move Conn to EventLoop

消除 Transport 路径与 EventLoop 路径对同一 MTcpConnection 的 alias 持有，
修复 OnClose 触发的 double-free coredump。

- MakeShared<MServerConnection>(IncomingConfig, Conn) 替代 AdoptConnection
- EventLoop.RegisterConnection 第二个参数 std::move(Conn) 让 EventLoop 独占

Refs: Docs/superpowers/specs/2026-07-10-drop-adopt-connection-design.md"
```

---

## Task 4: 端到端验证（不再 coredump + 现有套件不回归）

**Files:**
- 不改任何源文件

**Context:**
- 之前栈帧里 `MTcpMessageChannel::~MTcpMessageChannel` 内 inline 展开触发 `~MTcpConnection` 的 `double free or corruption (out)`，必须证明这个路径不再触发。
- 验证手段：(a) EchoService 启动 → accept 入站连接 → 对端关闭 → 进程不 abort；(b) 现有 validation suite 全绿。

- [ ] **Step 1: 重新构建并启动 EchoService + 一个 peer，触发关闭路径**

```bash
cd /root/Mession
cmake --build Build -j4
```

启动 EchoService：

```bash
./Build/Bin/EchoService --listen=17001 --inst=1 --actors=1001 --service=MEchoService --local-type=Echo &
ECHO_PID=$!
sleep 1
# 触发对端关闭：连接 17001 然后立即断开
(echo "ping"; sleep 0.1) | timeout 2 nc 127.0.0.1 17001 || true
# 给 OnClose / Disconnect 跑完时间
sleep 1
# 检查进程是否还活着，没 SIGABRT 退出
if kill -0 $ECHO_PID 2>/dev/null; then
    echo "OK: EchoService still running (no crash)"
    kill $ECHO_PID
    wait $ECHO_PID 2>/dev/null
else
    echo "FAIL: EchoService crashed"
    exit 1
fi
```

预期：`OK: EchoService still running (no crash)`。进程在 OnClose → Disconnect → Transport.reset → EventLoop.erase 这一连串析构后仍然存活（之前会在 `~MTcpConnection` 处 SIGABRT）。

- [ ] **Step 2: 跑现有 validation 套件确认无回归**

```bash
cd /root/Mession
python3 Scripts/validate.py --build-dir Build --no-build 2>&1 | tee /tmp/validate-after.log
```

预期：所有 suite 全绿。如果有失败且失败信息与 AdoptConnection / MTcpMessageChannel / Transport 路径相关，回滚并检查是否动了不该动的地方；否则视为独立失败，不属于本计划范围。

- [ ] **Step 3: 列可用 suite 做一次 smoke 跑**

```bash
cd /root/Mession
python3 Scripts/validate.py --build-dir Build --no-build --list-suites
```

确认没有 suite 标 "missing" 或 "error"。

- [ ] **Step 4: 总结 commit（如有验证产物）**

```bash
cd /root/Mession
git status
git log --oneline -5
```

预期：最近 3 个 commit 即 Task 1、2、3。`git status` 干净（除可能的 Build/ 编译产物，已在 .gitignore）。

---

## 自检（self-review）

**Spec 覆盖**：
- §1 目标 1（干掉 AdoptConnection）→ Task 1
- §1 目标 2（消除 EchoService 双持有）→ Task 3
- §1 目标 3（不破坏公共契约）→ Task 4 验证 + 不动其他 API
- §5 改动清单 3 个文件 → Task 1、2、3 全部覆盖
- §6 验证步骤 → Task 4

**Placeholder 扫描**：无 "TBD" / "TODO" / "类似 Task N"。

**类型一致性**：
- `MServerConnection(const SServerConnectionConfig&, TSharedPtr<INetConnection>)` 在 Task 2 定义，Task 3 使用，签名一致。
- `MakeShared<MTcpMessageChannel>(std::static_pointer_cast<MTcpConnection>(InConn))` 从原 AdoptConnection 沿用，参数类型一致。
- `std::move(Conn)` 在 Task 3 Step 2 使用，Conn 来源是 OnAccept 形参（按值接收的 `TSharedPtr<INetConnection>`），可移动。
