# PROJECT STATUS — 2026-08-20(会话交接)

> 本文档用于会话间交接:清空上下文后,读本文件即可恢复项目全貌。
> 冲突时以**代码 + CLAUDE.md + TODO.md** 为准。

## 一句话

Mession PoC **核心全部完成、全绿稳定**;F0 ActorMember 框架已实现(未提交),业务落地进入 F1(成员业务验证)。

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
| 仓库卫生/注释/文档 | ✅ 远端旧分支删除、过时注释修正、CLAUDE.md Active gaps 更新 |
| C2-C5 全量 format | ⏭️ **已决定跳过**(TODO.md 标注) |

**最近提交**:`6e45bf2`(inl 拆分)、`00efb93`(协议消息+序列化修复)、`81ea3a7`(enum 反射)、`b3314c5`(TODO 标注)。

### 1.2 F0 ActorMember 框架(2026-08-20 实现,⏳ 未提交,决策点 A 落地)

| 项 | 说明 |
|---|---|
| `EClassKind` 扩展 | ✅ `Actor = 5` / `ActorMember = 6`(Class.h) |
| 继承序列化 | ✅ `WriteSnapshot/ReadSnapshot/ByDomain` **先父后子递归** + `ReadSnapshotFields` 共享游标(Class.cpp);生成器 `SetParent` 接线(MSTRUCT 父类 → 注册函数,MCLASS → StaticClass) |
| `IActorMember` 运行时 | ✅ `IMemberHost`/`IActorMember : MObject`(Attach/Detach/GetHost/`GetActorMember<T>`)/`TMemberHostImpl` 成员表(Common/Runtime/Actor/ActorMember.h/.cpp) |
| 成员分发器 | ✅ `DispatchFrameworkMemberCall(FunctionId, Payload)`:查 `GMemberRpcEntries` → 反序列化取 PlayerId → `MActorSystem::Find` → 宿主成员表 → 复用生成 ServerCallHandler 反射调用 |
| MHeaderTool 生成 | ✅ `Type=Actor/ActorMember` 映射(顺带修复 `ExtractMacroValue` 空格 bug——此前 `Type = Service` 等**全部**落成 Object)+ `GMemberRpcEntries` 表生成(MemberRpcManifest.mgenerated.cpp,编入 mession_common)+ ServerCall handler 对 `TByteArray` 响应直接透传(修 `BuildPayload(TByteArray)` 返回空) |
| EchoService 验证 | ✅ `FMemberDispatchRequest` 入口 + demo 玩家 actor(`MEchoPlayerActor`,PlayerId=10001)+ 背包成员(`MPlayerItemContainer`)挂载注册 |
| 协议消息 | ✅ `Protocol/Messages/Player/FPlayerMessages.h`(`FPlayerRequestBase{PlayerId}` ← `FPlayerUseItemRequest`,继承序列化验证) |
| 单测 | ✅ `MemberFrameworkTest` 5/5(继承 round-trip/查表 id=7035/挂载+GetActorMember/分发 round-trip/FindActorMember) |
| 端到端 | ✅ 客户端→Gateway→`FrameworkMemberDispatch`→分发器→`UseItem`,返回 `bOk=1 Remaining=3` |

## 2. 验证基线(全绿)

- 全量构建绿(mession_echo/Gateway/MServiceRegistry/测试目标;**LuaBindTest 已修复**——`Lua/Tests/CMakeLists.txt` 补 `mession_generated_shared` 链接,原缺 `MTestBindClass::StaticClass()` 符号)
- 全量测试绿:AwaitCodegenTest 32/32、LogTest 94/94、ProtocolMessagesTest ALL OK、**MemberFrameworkTest 5/5**、**Lua 测试 ×11 全 ALL OK**
- validate 6/6 PASSED;verify_protocol.py All protocol checks passed
- **唯一未绿**:`A2DiffTest` 10/12(标注 A3;AST 路径修复 Type 映射/SetParent 后其 AST vs legacy 基线差异预期扩大,非门禁)

## 3. 工作区未提交(分两类,勿混提)

**A. F0 框架改动(本次会话产出,待提交)**:
- `Source/Common/Runtime/Actor/`(新):`ActorMember.h/.cpp`、`MemberRpcManifest.generated.h/.cpp`、`Tests/MemberFrameworkTest.cpp`
- `Source/Protocol/Messages/Player/FPlayerMessages.h`(新)
- `Source/Servers/EchoService/Members/`(新):`MPlayerItemContainer.h/.cpp`、`MEchoPlayerActor.h`
- 修改:`Class.h/.cpp`、`EchoService.h/.cpp`、`CMakeLists.txt`、`Source/Tools/MHeaderTool/`(ASTReflectionVisitor.cpp、Core/Types.h、Generation/CodeGenerator.{h,cpp}、Generation/ManifestGenerators.h、MHeaderTool.cpp)

**B. 用户/环境文件(勿与 A 混提)**:
- `reasonix.toml`(环境权限配置)
- `Source/Common/Script/Lua/CMakeLists.txt` + `MTestBindClass.cpp`(你的 Lua 测试类配套;`MTestBindClass.h` 已提交)
- `Source/Common/Script/Lua/Tests/CMakeLists.txt`(本次修复加了 1 行 `mession_generated_shared` 链接)+ `TestLuaBind.cpp`

## 4. 当前决策点(F0 已落地,下一步形态)

**方案 A(F0 ActorMember 框架)已完成**(§1.2):成员分发器/GetActorMember/协议下沉到成员/继承序列化,已在 EchoService 拓扑上以背包成员端到端验证。下一步:

- **F1. 框架验证成员铺开(推荐)**:登录/背包作为 `ActorMember` 全链路——可先在 EchoService 上再加属性/任务成员验证 `GetActorMember` 成员间协作 + 成员间 `TAwaitable`(成员函数指针,F0 已备),或直接进入 B
- **B. 建 MPlayerService**(player-design 目标架构):零业务协议 + actor 容器 + ActorMember——工程大,拓扑变化(需 `EServerType::Player`、`MPlayerActor` 正式化、Registry 按 PlayerId 粘性路由)
- **C. EchoService 上加更多业务 demo**(房间/战斗消息链路)——最贴近当前 PoC

> 相关设计:`Docs/superpowers/specs/2026-08-14-player-design.md` / `actor-member-framework.md` / `player-session-db-design.md`。
> 已就绪的地基:F0 框架 ✓、TAwaitable 成员函数 ✓、动态 actor ✓、真实消息 ✓、CallClient 下行 ✓。

## 5. 下一步建议

1. 提交 §3A 的 F0 框架改动(与 §3B 用户文件分离);建议同步更新 CLAUDE.md(Active gaps:`MClientManifest` 仍 stub,Gateway 未按 member manifest 路由——F0 分发是进程内入口,未改 Gateway)
2. 定 F1/B/C(§4)
3. 若选 F1:在 EchoService 上加第二个成员(属性/任务)验证 `GetActorMember` 成员间调用,或把 UseItem 链路补进 validate.py 套件(当前 6/6 未覆盖 member 分发)
