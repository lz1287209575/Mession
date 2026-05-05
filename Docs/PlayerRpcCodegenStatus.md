# MFunction(Async, PlayerRPC) 开发进度

## 提交状态

**已推送：** `1879171` — Implement MFunction(Async, PlayerRPC) codegen with ParaMeta validation

12 files changed, 597 insertions(+), 186 deletions(-)

## 已完成工作

### 1. MHeaderTool 代码生成增强

| 改动 | 文件 | 说明 |
|------|------|------|
| `__MFUNC__` 识别 | `Source/Tools/MHeaderTool.cpp` | 在宏匹配中支持 `__MFUNC__` |
| `MFunction` 小写分支 | `Source/Tools/MHeaderTool.cpp` | 兼容 `__MFUNC__(..., MFunction(...))` 嵌套写法 |
| 尾部 `}` 修复 | `Source/Tools/MHeaderTool.cpp` | inline body 声明传给 `ParseFunctionDeclaration` 前去掉 `}` |
| 协议头补全 | `Source/Tools/MHeaderTool.cpp` | 生成的头文件包含所有需要的 Protocol 消息类型 |

### 2. 反射宏补全

| 文件 | 改动 |
|------|------|
| `Source/Common/Runtime/Reflect/Reflection.h` | 新增 `#define __MFUNC__(...)` 和 `#define MFUTURE(...)` 两个 no-op 宏 |

### 3. PlayerLogout 实验迁移

| 文件 | 改动 |
|------|------|
| `Source/Servers/World/Player/PlayerLogout.h` | 新建，包含 `BuildPlayerOnlyResponse<T>` 模板 |
| `Source/Servers/World/Player/PlayerLogout.cpp` | 简化为辅助函数（`BuildPlayerOnlyResponse`、`ToProtocolPersistenceRecords`） |
| `Source/Servers/World/Player/PlayerService.h` | `PlayerLogout` 改为 `__MFUNC__(Async, PlayerRPC, ParaMeta=(PlayerId=NotZero), Dependencies=(Persistence, Mgo))` inline body |

### 4. PlayerService.h 27 个 RPC 标记完成

所有 27 个 RPC 函数均已添加 `MFUNCTION(ServerCall)` 标记，`PlayerLogout` 使用新的 `__MFUNC__` inline body 写法。

---

## 当前状态

**编译尚未验证。** 上次 `--clean-first` 构建后，MHeaderTool 删除了旧的 generated 文件但未能在 MSBuild 并行编译启动前全部重新生成，导致大量 `Cannot open source file` 错误。

需要重新执行：

```bash
cmake --build Build --clean-first -j4
```

预期可能的新错误：
- `ToProtocolPersistenceRecords` 在 `PlayerService.h` 的 inline body 中调用，但定义在 `PlayerLogout.cpp` 的匿名 namespace
- `MPlayerService.mgenerated.h` 中 `Request {};` 结构体语法是否已修复

---

## 后续计划（Plan 文件）

### 高优先级

1. **验证编译** — `cmake --build Build --clean-first -j4` 零错误
2. **修复 `ToProtocolPersistenceRecords` 可见性** — 将其从匿名 namespace 移到命名 namespace，或移到头文件
3. **验证 `MPlayerService.mgenerated.h`** — 确认 `Request {};` bug 已消除

### 中优先级

4. **EventBus 事件总线** — `Source/Common/Runtime/Event/MEvent.h` 系列文件已存在于 untracked
5. **MTimerManager 集中定时器** — `Source/Common/Runtime/Timer/` 尚未创建
6. **Windows Fiber 后端** — `FNullFiberBackend` 替换为 `FWindowsFiberBackend`

### 远期

7. **MEventAwait.h** — `co_await` 风格的 EventBus 订阅桥接
8. **更多 RPC 迁移** — 在验证 `PlayerLogout` 成功后，将其他编排类 RPC 迁移到 `__MFUNC__` 写法

---

## 新增未跟踪文件（untracked）

```
Docs/AsyncAwaitProposal.md
Docs/AsyncAwaitTasks.md
Docs/FrameworksResearch.md
Source/Common/Runtime/Async/
Source/Common/Runtime/Event/
Source/Servers/App/MObjectCallRouter.h
Source/Servers/App/MRouterRegistry.h
Source/Tools/MObjectEditorAvalonia/obj/
```

这些文件尚未提交，建议确认是否需要纳入版本控制。

---

## 相关文档

- [PlayerRpcDevelopment.md](Docs/PlayerRpcDevelopment.md) — 当前已落地的 Player RPC 开发约定
- [AsyncAwaitProposal.md](Docs/AsyncAwaitProposal.md) — async/await 设计提案（草稿）
- [AsyncAwaitTasks.md](Docs/AsyncAwaitTasks.md) — async/await 实现任务拆分（草稿）
- [FrameworksResearch.md](Docs/FrameworksResearch.md) — ET + skynet 框架研究（草稿）
