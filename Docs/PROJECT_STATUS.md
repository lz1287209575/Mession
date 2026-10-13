# PROJECT STATUS — 2026-10-13(会话交接)

> 本文档用于会话间交接:清空上下文后,读本文件即可恢复项目全貌。
> 冲突时以**代码 + CLAUDE.md + TODO.md** 为准。

## 一句话

Mession PoC 核心功能全绿稳定;F0 ActorMember 框架已提交(`c589c73`,= 当前 `origin/main` tip);
仓库已完成一轮**格式 + 注释 + 结构**重构的 **Phase 0 / 1 / 2**(§1.3),工作树干净,
**23 个提交待推送**(22 个重构提交 + 本文档更新);仅剩 **Phase 3(超大文件拆分)**。

## 1. 已完成

### 1.1 已提交推送 main(全部绿)

| 项 | 说明 |
|---|---|
| validate 全链 | ✅ 6/6(chain_local/remote/remote_async/error/downlink/dynamic_actor) |
| 跨实例 actor 路由 | ✅ 动态 actor 注册 → Registry ActorIds 上报 → 对端路由(`MActorSystem::Register`/`MActorRouter::RegisterActor` 完整封装) |
| MClientManifest 真生成 | ✅ ServerCall 表 + `MFUNCTION(CallClient)` 下行 manifest + 服务端调用 stub(`MDownlinkCall_<Class>_<Func>`) |
| server→client 推送 | ✅ Gateway PushClientDownlink(connId=0 广播)+ MClientTargetResolver 接线 |
| P5 TAwaitable | ✅ 状态机 codegen + **成员函数指针**(std::invoke) |
| 协议真实消息 | ✅ `FBattleMessages.h`/`FSceneMessages.h` + `ProtocolMessagesTest` round-trip |
| **反射嵌套 MSTRUCT 序列化修复** | ✅ `TProperty::WriteValue` 由 WriteBytes 浅拷贝 → `SerializeArchiveValue` 递归(修复析构 double free,ASAN 定位) |
| namespace enum 反射 | ✅ QualifiedName 注册 + `MEnumRegistration` 引导 + FindEnum 惰性 |
| CPU 忙转修复 | ✅ `NetEventLoop` 空 fd `sleep_for`;EchoService `--subs` 可配置(默认 4) |
| ReflectionPropertyTemplates.inl 拆分 | ✅ 1977→551 主文件 + BinaryJson/StringExport 子文件 |
| C2-C5 全量 format | ⏭️ **已决定跳过**(TODO.md 标注) |

**历史提交**:`6e45bf2`(inl 拆分)、`00efb93`(协议消息+序列化修复)、`81ea3a7`(enum 反射)、`b3314c5`(TODO 标注)。

### 1.2 F0 ActorMember 框架(已提交,落在 `c589c73`)

| 项 | 说明 |
|---|---|
| `EClassKind` 扩展 | ✅ `Actor = 5` / `ActorMember = 6`(Class.h) |
| 继承序列化 | ✅ `WriteSnapshot/ReadSnapshot/ByDomain` **先父后子递归** + `ReadSnapshotFields` 共享游标(Class.cpp);生成器 `SetParent` 接线(MSTRUCT 父类 → 注册函数,MCLASS → StaticClass) |
| `IActorMember` 运行时 | ✅ `IMemberHost`/`IActorMember : MObject`(Attach/Detach/GetHost/`GetActorMember<T>`)/`TMemberHostImpl` 成员表(Common/Runtime/Actor/ActorMember.h/.cpp) |
| 成员分发器 | ✅ `DispatchFrameworkMemberCall(FunctionId, Payload)`:查 `GMemberRpcEntries` → 反序列化取 PlayerId → `MActorSystem::Find` → 宿主成员表 → 复用生成 ServerCallHandler 反射调用 |
| MHeaderTool 生成 | ✅ `Type=Actor/ActorMember` 映射(顺带修复 `ExtractMacroValue` 空格 bug——此前 `Type = Service` 等**全部**落成 Object)+ `GMemberRpcEntries` 表生成(MemberRpcManifest.mgenerated.cpp,编入 mession_common)+ ServerCall handler 对 `TByteArray` 响应直接透传(修 `BuildPayload(TByteArray)` 返回空) |
| EchoService 验证 | ✅ `FMemberDispatchRequest` 入口 + demo 玩家 actor(`MPlayerActor`,PlayerId=10001)+ 背包成员(`MPlayerItemContainer`)挂载注册 |
| 协议消息 | ✅ `Protocol/Messages/Player/FPlayerMessages.h`(`FPlayerRequestBase{PlayerId}` ← `FPlayerUseItemRequest`,继承序列化验证) |
| 单测 | ⚠️ `MemberFrameworkTest` 当前 `rc=134`(abort)。设计已由用户裁定为**过时/废弃**,不再作为门禁(见 §2) |
| 端到端 | ✅ 客户端→Gateway→`FrameworkMemberDispatch`→分发器→`UseItem`,返回 `bOk=1 Remaining=3` |

### 1.3 代码库重构 Phase 0 / 1 / 2(2026-10-13)

目标:解决"代码和注释结构非常混乱"。范围 = **格式 + 注释规范化 + 结构拆分**,分阶段提交、每阶段独立验证。

| 阶段 | 内容 | 提交 |
|---|---|---|
| Phase 0 | 补缺失标准头文件;修 `Scripts/check-style.sh` 工具探测、`Source/ThirdParty/` 豁免、pre-commit clang-format 版本对齐;修 Lua 绑定层栈泄漏 / `__pairs` 返回序 / `lua_isstring`→`lua_type` / Invoke 前切读模式 | 3 |
| Phase 1 | clang-format-19 全量落地 **88** 个非合规项目文件 | 1 |
| Phase 2a | 注释盘点 + 判定工具链(`code_identical.py` 等四件套) | 0 |
| Phase 2b | 按子系统分批注释重写:**199** 个文件 → 中文 Doxygen<br>(Log 44、Runtime 核心 30、Net/Rpc 9、IO/ServiceDiscovery/Event/Math/Containers/Validation 21、Protocol 8、Examples 21、Script 58、Serialization+Tools 桩 6、Async 补漏、Log 补齐) | 11 |
| Phase 2b(续) | Net 18 + Runtime(Reflect/Object/EventLoop/Asset)27 + Tools/MHeaderTool 29 + Servers 18 = **92** 个文件 | 4 |
| Phase 2c | 全仓复核 + 零散英文散文注释收尾(8 文件 15 行)+ `CodeGenerator.h` 注释重写与补 `@file`(2655 行、172 处英文注释、新增 8 条 NOTE)+ `MRpcChannel.cpp` 遗漏英文行 | 3 |
| **合计** | | **22** |
| Phase 3 | 超大文件拆分 | ⏳ **未开始** |

**Phase 2 的做法**:不是机械加 `@file` 头,而是**按代码真实语义重写**注释。技术手段是
"剥离注释后与 HEAD 逐字符比对"(`code_identical.py`),它对"注释未闭合吞掉代码"最敏感。
凡发现注释与实现不符的,一律**以代码为准**改注释,并就地加 `NOTE:` 记录(全仓共 **78 处**)。
代码本身不改——发现的真实缺陷留待单独修复(§5)。

**注释语言基准**:`Docs/CodingStyle.md` §7.0(行 451 起)。标记词(TODO/FIXME/NOTE)英文 +
正文中文;上游/协议/编译器专有名词保留原文;`} // namespace Xxx` 收尾标记保持英文
(`.clang-format` `FixNamespaceComments: true` 会重算);`/// @lua-*` 机器注解必须与 HEAD 逐字一致。
**ABI 红线**:`MPROPERTY()` 字段名/顺序/类型是公开 ABI,不得重命名、重排、删除。

## 2. 验证基线

- **构建**:`cmake-build-debug`(Debug,g++ 12.2.0)+ ninja,`BUILD_RC=0`;`Build/`(Release,
  Unix Makefiles)同样可用,用于端到端验证。
- **测试**:`Bin/` 下 **23 PASS / 0 FAIL / 2 SKIP**(AwaitCodegenTest 32/0、LogTest 94/0、
  StringUtilsTest 25/0、Lua 测试 ×11 ALL OK、ProtocolMessagesTest、AsyncDemoTest、
  MHeaderTool_test rc=0 等)。
  `A2DiffTest` 的工作方式是逐文件比对 AST 与 legacy 两条生成路径的结果,无差异时静默通过
  (输出 `0 passed, 0 failed`,rc=0),不是断言计数型测试。
- **已知遗留失败(重构前即失败,非回归)**:
  - `ASTDumpTest` — 超时(`rc=124`)
  - `MemberFrameworkTest` — `rc=134`(用户裁定过时/废弃)
- **格式**:`bash Scripts/check-style.sh` → `OK - all files conform to .clang-format`(296 文件)。
  clang-format-19 独立复核:298 个受检项目文件中 **0 违规**。
- **文件头**:`@file` 覆盖 **298/298**。
- **注释定界符**:全仓平衡;唯一 `UNBALANCED` 是 `Tools/MHeaderTool/Util/StringUtil.h` 的
  假阳性(注释正文里合法地引用了 `/* *\/` 写法)。
- **validate 6/6 PASSED**(在 `Build/` Release 下);verify_protocol.py All protocol checks passed。
  ⚠️ **Debug 下 `validate.py` 的 Test 2b(`chain_remote_async`)确定性失败**,根因是
  `Promise.h:182` 的 `assert(false && "deadlock risk: SetValue on loop thread")` —— 该 assert
  由 `3913466`(已在 `origin/main`)引入,`d7a97a5` 补 `<cassert>` 后于 Debug 真正展开。
  属**既有 Debug 红线段,非本轮回归**;因此 Debug 构建不用于端到端验证。

## 3. 工作区状态

**干净**(`git status --porcelain` 无输出)。

`main` 领先 `origin/main`(`c589c73`)共 **23 个提交**,尚未推送;其中含一次历史改写
(filter-branch 移除了误入 `0bc2b5d` 的 4 个 macOS AppleDouble 文件,仅影响未推送区间,
树内容不变:`a127021…`)。

## 4. 当前决策点(F0 已落地,下一步形态)

**方案 A(F0 ActorMember 框架)已完成并提交**(§1.2):成员分发器/GetActorMember/协议下沉到成员/继承序列化,已在 EchoService 拓扑上以背包成员端到端验证。下一步:

- **F1. 框架验证成员铺开(推荐)**:登录/背包作为 `ActorMember` 全链路——可先在 EchoService 上再加属性/任务成员验证 `GetActorMember` 成员间协作 + 成员间 `TAwaitable`(成员函数指针,F0 已备),或直接进入 B
- **B. 建 MPlayerService**(player-design 目标架构):零业务协议 + actor 容器 + ActorMember——工程大,拓扑变化(需 `EServerType::Player`、`MPlayerActor` 正式化、Registry 按 PlayerId 粘性路由)
- **C. EchoService 上加更多业务 demo**(房间/战斗消息链路)——最贴近当前 PoC

> 相关设计:`Docs/superpowers/specs/2026-08-14-player-design.md` / `actor-member-framework.md` / `player-session-db-design.md`。
> 已就绪的地基:F0 框架 ✓、TAwaitable 成员函数 ✓、动态 actor ✓、真实消息 ✓、CallClient 下行 ✓。

## 5. 下一步

1. **Phase 3:拆分超大文件**。注释重写后 >600 行的项目文件共 **8** 个:
   `Generation/CodeGenerator.h` **2655**、`Reflect/ReflectionArchiveInvoke.inl` 874、
   `Reflect/ReflectionPropertyTemplates.BinaryJson.inl` 817、`Reflect/Reflection.h` 796、
   `Script/Lua/LuaEngine.cpp` 743、`MHeaderTool/AST/ASTReflectionVisitor.cpp` 725、
   `Generation/ManifestGenerators.h` 722、`Net/ServiceDiscovery/EndpointCache.cpp` 682。
   拆分策略需与用户确认;注释重写已先行完成,可直接进入拆分。
2. **裁定 78 处 `NOTE:` 中标记的真实代码缺陷**是否另开修复任务。已确认的代表性条目:
   - `MEvent` 所有事件类型 ID 恒为 1(静态计数器在函数模板内,每实例化一份)→ `MEventBus` 把所有事件当同一键路由
   - `MT_ServerPush` ack 解析偏移错 2 字节(生产端有 `[Reserved:2B]`,消费端按无 Reserved 解析)
   - `MEventBus` 一次性订阅触发后**整表清除**,本轮未轮到触发的一次性订阅失去自动退订能力
   - `MEventBus` 首个订阅者句柄为 0,被 `IsValid()` 判无效 → 句柄 0 无法退订
   - `MessageUtils.h` 长度字段强转 `uint16`,>65535 静默截断
   - `EFunctionFlags::NetServer`/`NetClient` 被 `BuildFunctionFlagsExpr` 当"标签"跳过,
     `MFUNCTION(...)` 无法设置(当前无调用点,潜伏)
   - `EmitForwardDeclaration` 未过滤基础类型(唯一调用点传类名,未暴露)
   - Lua 侧:`M.GetObject`/`M.LiveObjects`/`RPC.callById` 未实现、`MLuaBridge` int64 返回值截断为 int32、
     `MLuaVector`/`MLuaMap` `size()` 截断、`Save/RestoreLuaState` 无 C++ 反射 fallback、
     `MetaLen` 截断、`GBridge` 文件级静态、`LUA_EXTRASPACE` 实为 `sizeof(void*)`
   - `GatewayServer.cpp:287` 空 else 分支疑似遗留;`ServiceRegistryServer.h:106` `NextSeq` 从未读写
3. **推送 `main`**(23 提交)。
4. 可选:同步更新 `CLAUDE.md` 的 Active gaps(`MClientManifest` 仍 stub,Gateway 未按 member manifest 路由)。
5. 明确**不在重构范围**:`Projects/`、`reasonix.toml`、`Source/Vendor/ImGui` 等非代码/用户文件;
   `Source/ThirdParty/` 从不修改且不在风格检查范围;`Build/Generated/**` 与 `*.generated.h` 一律跳过。
