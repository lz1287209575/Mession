# PROJECT STATUS — 2026-08-20(会话交接)

> 本文档用于会话间交接:清空上下文后,读本文件即可恢复项目全貌。
> 冲突时以**代码 + CLAUDE.md + TODO.md** 为准。

## 一句话

Mession PoC **核心全部完成、全绿稳定**;正在起步"业务"(落地形态待决策,见 §4)。

## 1. 已完成(全部提交推送 main)

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

## 2. 验证基线(全绿)

- 全量构建 100% 绿(含 mession_lua/测试目标)
- 全量测试绿(AwaitCodegenTest 32/32、LogTest 94/94、Lua 全 OK、ProtocolMessagesTest ALL OK)
- validate 6/6 PASSED
- **唯一未绿**:`A2DiffTest` 10/12(`SEchoServiceConfig`/`MEchoService` 的 AST vs legacy 基线差异,标注 A3;未修)

## 3. 工作区未提交(你的/环境,勿混提)

- `reasonix.toml`(环境权限配置)
- `Source/Common/Script/Lua/CMakeLists.txt` + `MTestBindClass.cpp`(你的 Lua 测试类配套;`MTestBindClass.h` 已提交——补了 MGENERATED_BODY,shared 组编译必需)
- `Source/Common/Script/Lua/Tests/CMakeLists.txt` + `TestLuaBind.cpp`

## 4. 当前决策点(被中断,待用户定)

**"开始做业务"的落地形态**(player-design.md 是目标架构:LoginService + MPlayerService + MPlayerActor + ActorMember):

- **A. 先 ActorMember 框架**(推荐):成员分发器/GetActorMember/协议下沉到成员,在现有 EchoService 拓扑上验证(如背包/属性成员)——地基,不引入新进程
- **B. 直接建 MPlayerService**(player-design 目标架构):零业务协议 + actor 容器 + ActorMember——工程大,拓扑变化
- **C. EchoService 上加业务 demo**(房间/战斗/背包消息链路)——最贴近当前 PoC

> 相关设计:`Docs/superpowers/specs/2026-08-14-player-design.md` / `actor-member-framework.md` / `player-session-db-design.md`。
> 已就绪的地基:TAwaitable 成员函数 ✓、动态 actor ✓、真实消息 ✓、CallClient 下行 ✓。

## 5. 下一步建议

1. 定业务落地形态(§4)
2. 若选 A:先实现 ActorMember 框架(成员分发器 + GetActorMember + 一个业务成员端到端)
3. 若选 B:先建 MPlayerService 骨架 + 首个 ActorMember 成员
