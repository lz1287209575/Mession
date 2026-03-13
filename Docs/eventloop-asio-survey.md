# EventLoop / Asio 风格网络层接入调研与计划

## 一、调研摘要

### 1. Asio 风格（Boost.Asio / Standalone Asio）

- **核心**：`io_context` 驱动，所有 I/O 通过 `async_accept` / `async_read` / `async_write` 等提交，在 `io_context.run()` 中回调执行。
- **特点**：
  - 跨平台：Linux 下可用 epoll，Windows 下 IOCP，macOS 下 kqueue，由 Asio 封装。
  - **Standalone Asio**：可无 Boost 依赖（`ASIO_STANDALONE`），仅头文件 + 少量源文件，适合用 CMake FetchContent 或拷贝进项目。
  - 连接对象通常继承 `enable_shared_from_this`，在异步回调中通过 `shared_from_this()` 保活。
- **常见模式**：
  - **Lambda 回调**：`acceptor_.async_accept(socket_, [this](error_code ec) { ... do_accept(); });`
  - **C++20 协程**：`co_spawn` + `awaitable` + `use_awaitable`，代码更线性。
- **性能注意**（来自社区与 Issue）：
  - **Linux**：多线程共跑同一个 `io_context.run()` 时，epoll 的锁竞争可能导致吞吐下降；更推荐「一个 io_context 一个线程」或「多 io_context 分片」。
  - **Windows**：单 io_context + 多线程跑 `run()` 利用 IOCP 往往更好。
  - 游戏服若连接数大，需要按平台选线程模型并做压测。

### 2. 自研 EventLoop / Reactor

- **Reactor 模式**：单线程（或固定线程）事件循环：`poll`/`epoll_wait` → 得到就绪 fd → 按 fd 分发到对应 Connection/Session 处理读/写/断线。
- **与当前实现的关系**：我们已有 `MSocketPoller`（BuildReadableItems + PollReadable）+ `MTcpConnection`（非阻塞收发、粘包）。在不动底层的前提下，可以再封一层「EventLoop」：统一 `poll` 调用、按 ConnectionId 分发到注册的回调（OnRead / OnClose），并可选加入定时器、跨线程 post。
- **零依赖**：不引入 Asio，仅用现有 `poll`（或后续换成 epoll）即可实现单线程 Reactor；多线程可用 one-loop-per-thread 或固定 worker 池。
- **参考**：muduo 风格、Tiny-Network、eomaia 等均为 C++11 起、无 Boost 的 Reactor 实现，可借鉴接口设计（Channel、EventLoop、TcpServer），不必照搬实现。

### 3. 与当前 Mession 网络层的对应关系

| 当前 | Asio 风格 | 自研 EventLoop |
|------|-----------|----------------|
| `MSocket::CreateListenSocket` + `Accept` | `asio::ip::tcp::acceptor` + `async_accept` | Loop 内 `accept()` + 回调 `OnAccept(conn)` |
| 各服手写「BuildReadableItems → PollReadable → 遍历结果处理」 | `io_context.run()` 内按完成事件回调 | `Loop.Run()` 内 `poll` → 按 fd 调 `OnRead(conn)` / `OnClose(conn)` |
| `MTcpConnection` 非阻塞 + 缓冲 | `asio::ip::tcp::socket` + `async_read_some` / 自定义协议层 | 仍用 `MTcpConnection`，Loop 只负责「可读时调 ProcessRecvBuffer / 可写时 FlushSendBuffer」 |
| `MServerConnection` 握手/心跳/重连 | 可放在 Asio 的 async 链里或单独 timer | 可放在 Loop 的定时器或 Tick 回调里 |

---

## 二、方案对比

| 维度 | 方案 A：接入 Standalone Asio | 方案 B：自研轻量 EventLoop | 方案 C：Asio 风格 API 薄封装（底层仍 poll） |
|------|-----------------------------|----------------------------|---------------------------------------------|
| **依赖** | 引入 Asio 头/库（无 Boost） | 零外部依赖 | 零外部依赖 |
| **API 风格** | 原生 Asio（async_* + io_context） | 自研 Run/Register/OnRead/OnAccept | 类 Asio 的「注册 + 回调」语义 |
| **与现有代码** | 需逐步用 Asio socket 替代 MTcpConnection | 保留 MTcpConnection，外层加 Loop 与注册 | 保留 MTcpConnection + poll，只加一层「异步风格」接口 |
| **跨平台 / 高性能** | 直接受益 epoll/IOCP/kqueue | 需自行封装 epoll（后续可加） | 先保持 poll，后续可换实现 |
| **工作量** | 大（替换传输层 + 各服循环） | 中（新增 Loop + 各服迁到注册） | 小（薄封装 + 各服可选迁移） |
| **风险** | 线程模型与性能需验证 | 自研 bug 与边界情况 | 低，行为与现有一致 |

---

## 三、推荐路线与分阶段计划

### 推荐：优先方案 B，预留方案 A 可能

- **理由**：项目已强调零依赖（如日志），且当前 Phase 1 传输层稳定；先做「自研 EventLoop」可在不引入新依赖的前提下，统一各服的 poll 循环、减少样板、为以后换 epoll 或引入 Asio 留接口。
- **若后续希望直接用成熟生态、接受 Asio 依赖**：再评估方案 A，并可与方案 B 的「Loop + 回调」抽象对齐，便于逐步替换底层。

---

### 阶段 1：自研 EventLoop 抽象（方案 B 第一步）

**目标**：在现有 `MSocketPoller` + `MTcpConnection` 之上增加「单线程 EventLoop」，各服从「手写 poll 循环」改为「向 Loop 注册监听/连接 + 回调」。

1. **新增类型**（建议放在 `Core/` 或 `Core/Net/`）：
   - `MNetEventLoop`：单例或按服一个；提供：
     - `RegisterListener(port, OnAccept)`：内部 CreateListenSocket + 将 listen fd 纳入 poll。
     - `RegisterConnection(ConnectionId, TSharedPtr<INetConnection>, OnRead, OnClose)`：把连接加入 poll 集合，可读时调 OnRead，错误/断线时调 OnClose 并移除。
     - `RunOnce(timeoutMs)` / `Run()`：内部 BuildReadableItems（含 listen fd）+ PollReadable + 分发（accept 或 调 OnRead/OnClose）。
   - 可选：`MNetEventLoop::AddTimer(delayMs, callback)` 用于心跳、超时等，第一版可用简单「下次 RunOnce 前检查」的定时器队列。

2. **与现有组件关系**：
   - 仍使用 `MSocket::CreateListenSocket`、`MSocket::AcceptConnection`、`MTcpConnection`、`MLengthPrefixedPacketCodec`。
   - `MSocketPoller` 可保留，由 `MNetEventLoop` 内部调用；或把「构建 poll 列表 + poll + 分发」收进 Loop，Poller 仅负责「给定 fd 列表，返回就绪列表」。

3. **迁移策略**：Gateway / World / Login / Router 中，先选一个服（如 Login）改为「创建 MNetEventLoop → RegisterListener + 在 OnAccept 里 RegisterConnection → Run()」，验证通过后再迁其他服；Scene 无 listen，可只迁「连接 Router/World」为 RegisterConnection 或保持现有 Tick。

4. **验收**：现有 `scripts/validate.py` 全通过；各服行为与当前一致，仅实现从「手写 poll」改为「Loop + 回调」。

---

### 阶段 2：Asio 风格 API 可选（方案 C 或与 B 合并）

**目标**：让「注册 + 回调」的用法更接近 Asio 的 async 语义，便于以后若接 Asio 时业务层少改。

1. 在 EventLoop 上提供：
   - `AsyncAccept(listenFd, OnAccept(conn))`：等价于「listen fd 可读时 accept，并调 OnAccept」。
   - `AsyncRead(conn, OnPacket(payload))`：等价于「conn 可读时 ProcessRecvBuffer，每完整一包调一次 OnPacket」。
   - 命名与参数风格可参考 Asio（如 handler 用 `function<void(error_code, T)>`），底层仍用现有 poll + MTcpConnection。

2. 这样业务侧写的是「注册回调」而非「轮询 + if (IsReadable)」，后续若替换为真实 Asio，只需换 EventLoop 实现，不一定要改业务签名。

---

### 阶段 3（可选）：后端升级与 Asio 接入

- **后端升级**：在 EventLoop 内将「poll」替换为「epoll（Linux）/ kqueue（macOS）/ 继续 WSAPoll（Windows）」；接口保持不变，仅改 Loop 内部。
- **Asio 接入（方案 A）**：若决定引入 Standalone Asio，可：
  - 用 `asio::ip::tcp::acceptor` + `async_accept` 替代 `RegisterListener` 的 accept 路径；
  - 用 `asio::ip::tcp::socket` + 自定义 `async_read`（按 Length+Payload 读满一包）替代 `MTcpConnection` 的读路径；
  - 仍通过「OnAccept / OnRead / OnClose」与现有业务对接，或逐步把业务改为 Asio 的 completion token（如 use_awaitable）。
  - 需明确线程模型：Linux 建议一 io_context 一线程或多 io_context；Windows 可单 io_context 多线程。

---

## 四、小结与建议

| 阶段 | 内容 | 产出 |
|------|------|------|
| **1** | 自研 EventLoop（RegisterListener / RegisterConnection / Run） | 各服可迁到「Loop + 回调」，保留 MTcpConnection，零新依赖 |
| **2** | Asio 风格 API 薄封装（AsyncAccept / AsyncRead 语义） | 业务侧统一为「注册 + 回调」，为将来换 Asio 留口子 |
| **3（可选）** | 后端换 epoll / 或接入 Standalone Asio | 性能与跨平台能力增强，或直接使用 Asio 生态 |

建议先落地**阶段 1**，在 `docs/socket-layer-refactor.md` 或本文中记录「EventLoop 设计决策」和与现有网络层进度表的对应关系；阶段 2、3 按需排期。
