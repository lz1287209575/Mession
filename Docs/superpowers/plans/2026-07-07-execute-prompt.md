# PoC 第 1 步执行 Prompt

> 用途：复制下面整个代码块到新 Claude Code Session，作为初始 prompt。

---

我需要你执行一份实施计划：`/root/Mession/Docs/superpowers/plans/2026-07-07-actor-rpc-refactor.md`

请先通读完整份 plan（1500+ 行，6 个 Phase）。

## 强制规则

1. **任何修改前**先 `Skill` 调用 `superpowers:using-superpowers`，遵守 skill 优先级（process 技能先于 implementation 技能）
2. **Task 1.0 (ServiceId/InstId 位布局)** 是 Phase 1 的硬性前置——所有后续 Task 依赖 `MServiceId::Make/Get*`
3. **架构 v1 (MWorldServer / MBackendServiceEndpoint) 内容已废弃**，plan 头部的"约定"段明确写了跳过——不要执行 plan 里残留的 v1 文字说明
4. **PoC 第 1 步只做 1 个 Service（Echo）**——不要顺手实现 SampleB / 链 2 / 跨进程互连；plan 文末的"扩展步骤"是后续阶段
5. **lambda 嵌套规则**（plan 编码约定章节）：回调体 > 3 行 / 含 try-catch / 捕获 > 1 个变量 ⇒ 抽出命名函数；转发 lambda 限 1 行
6. **MObject 派生类**用 `NewMObject<T>(Outer, Name, ...)` 唯一入口；持有非托管资源实现 `IDisposable::Dispose()` 幂等释放
7. **每完成一个 Task** 在 plan 对应 checkbox 勾选；用 `TaskCreate/TaskUpdate`（TaskCreate 创建 + TaskUpdate 切 in_progress/completed）

## 执行流程

按 plan 里的依赖关系顺序执行：

```
Task 0.x (已完成，跳过)
    ↓
Task 1.0 (ServiceId/InstId 工具)  ← 第一个
Task 1.1 (删 WorldServer 目录)
Task 1.2 (删 ObjectCall*)
Task 1.3 (编译验证)  ← 用 cmake --build Build -j4 验证前 3 个 Task
Task 1.4 (删 4 服 target + 源码)
    ↓
Task 2.1 (FSampleEchoRequest/Response 协议消息)
Task 2.2 (MServiceContainer 轻量 Registry)
Task 2.3 (MEchoService 头文件)
Task 2.4 (MEchoService cpp)
Task 2.5 (EchoServiceMain 平铺 main)
Task 2.6 (CMakeLists.txt 添加 target)
Task 2.7 (编译验证)
    ↓
Task 3.1 (ClientFunctionRoute 表)
Task 3.2 (Gateway DispatchClientCall 实现 — 改 HandleClientPacket)
Task 3.3 (Gateway 启动时注册 Echo peer)
Task 3.4 (编译验证)
    ↓
Task 4.1 (Scripts/start_service.py — 参数化启动器)
Task 4.2 (验收 — 跑 start_service.py start --services=gateway,echo + 看 Logs/*.log)
    ↓
Task 5.0 (cleanup — 6 项 grep 全部符合期望)
    ↓
✅ PoC 第 1 步完成
```

Task 6 (MObject 改造) 是独立排期，本 Session **不执行**——除非我后续明确要求。

## 每 Task 工作模式

对每个 Task：

1. **读 Task 全文** — 注意 plan 里可能有"改动 1/2/3"多个步骤，按顺序执行
2. **执行前确认环境** — 用 `ls` / `cat` / `grep` 检查现状是否符合 plan 假设
3. **写代码** — plan 里的代码块是可直接粘贴的最终代码；如有不确定项先问我
4. **验证** — 编译 / 运行 grep / 起服务看日志
5. **勾选 checkbox** — 把 plan 里对应的 `- [ ]` 改成 `- [x]`
6. **TaskUpdate** — 把对应 TaskCreate 的 task 标 `completed`

## 关键文件位置速查

- 协议消息：`Source/Protocol/Messages/EchoService/FSampleEchoMessages.h`
- ServiceContainer：`Source/Servers/App/ServiceContainer.{h,cpp}`
- ServiceId 工具：`Source/Servers/App/ServiceId.h`
- MEchoService：`Source/Servers/EchoService/{EchoService.h,EchoService.cpp,EchoServiceMain.cpp}`
- 路由表：`Source/Protocol/Messages/Common/ClientFunctionRoute.h`
- 启动脚本：`Scripts/start_service.py`
- CMakeLists：`CMakeLists.txt`

## 决策点（不要犹豫，直接按 plan 走）

- **EServerType 枚举**：加 `Echo = 7`；`SampleB = 8` 注释为第 2 步扩展（不实现）
- **位布局**：`uint64 ActorId = [ServiceId: high 32][InstId: low 32]`
- **MEchoService 反射类型**：`MCLASS(Type=Service)`
- **本地 Actor 注册**：标 `EServerType::Unknown`（走 IsActorLocal 分支）
- **Transport 注册**：`MServiceContainer::Register(Conn)` + `RegisterRpcTransport(ServiceType, Conn)` 双注册
- **OnMessage 回调**：MEchoService 是栈对象不继承 TSharedFromThis，**PoC 不注册回调**（响应包由 MServerConnection::HandlePacket 内部消化）

## 完成后输出

执行完所有 Task（1.0 到 5.0）后，给我一个总结：

1. ✅ 哪些 Task 完成（带 checkbox 状态）
2. 📊 编译日志最后 20 行
3. 📊 `./Scripts/start_service.py start --services=gateway,echo` 的输出
4. 📊 `./Scripts/start_service.py status` 的输出
5. 📊 AC-1 ~ AC-4 哪几项通过
6. ⚠️ 任何与 plan 假设不符、需要 review 的地方