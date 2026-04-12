# UE 客户端接入 Agent Prompt

本文档提供一份可以直接发给 UE 侧 Agent 的执行 Prompt。

建议配合阅读：

- `Docs/UE_LoginIntegrationGuide.md`
- `Docs/UE_ClientSkeletonDesign.md`
- `Docs/UE_PlayerSyncAgentGuide.md`
- `Docs/UE_HeartbeatIntegrationGuide.md`

## 可直接复制的 Prompt

```text
你现在要在 Unreal Engine 工程里实现当前 Mession 主线协议的客户端接入，不要按旧的“最小登录样板”设计。

目标不是只做一次 Login smoke test，而是先搭一套可继续扩展的 UE 客户端基础层，能够承接当前主线的请求响应和场景下行。

一、网络与协议边界
1. 客户端只直连 TCP Gateway：127.0.0.1:8001
2. 所有 TCP 包都使用 length-prefixed framing：
   - uint32 PacketLength
   - PacketBody bytes
3. 当前主线客户端请求统一走 MT_FunctionCall = 13
4. 当前场景同步下行也走 MsgType = 13，但 downlink 不带 CallId
5. 当前协议按 little-endian 兼容现有实现
6. 不要引入额外第三方网络库，优先使用 UE 自带 Socket 能力

二、当前应按这组客户端 API 设计
1. Gateway 本地：
   - Client_Echo
   - Client_Heartbeat
2. World 转发：
   - Client_Login
   - Client_FindPlayer
   - Client_Move
   - Client_QueryProfile
   - Client_QueryPawn
   - Client_QueryInventory
   - Client_QueryProgression
   - Client_Logout
   - Client_SwitchScene
   - Client_ChangeGold
   - Client_EquipItem
   - Client_GrantExperience
   - Client_ModifyHealth
   - Client_CastSkill

三、这次至少先实现的能力
第一阶段：
1. TCP 连接与收发
2. MT_FunctionCall 请求/响应编解码
3. Client_Heartbeat
4. Client_Login
5. Client_FindPlayer
6. Client_QueryProfile

第二阶段：
1. Client_QueryPawn
2. Client_QueryInventory
3. Client_QueryProgression
4. Client_Move
5. Client_SwitchScene

第三阶段：
1. Client_ChangeGold
2. Client_EquipItem
3. Client_GrantExperience
4. Client_ModifyHealth
5. Client_CastSkill
6. Client_ScenePlayerEnter
7. Client_ScenePlayerUpdate
8. Client_ScenePlayerLeave

四、包体格式
1. 客户端请求/响应：
   - uint8 MsgType = 13
   - uint16 FunctionId
   - uint64 CallId
   - uint32 PayloadSize
   - Payload bytes
2. 客户端 downlink：
   - uint8 MsgType = 13
   - uint16 FunctionId
   - uint32 PayloadSize
   - Payload bytes

五、FunctionId 规则
1. Client API 使用：
   - ComputeStableReflectId("MClientApi", FunctionName)
2. Downlink 使用：
   - ComputeStableReflectId("MClientDownlink", FunctionName)
3. 不要长期手写 magic number，建议把稳定 ID 算法一并实现
4. 当前已验证的常用值：
   - Client_Heartbeat = 8809
   - Client_Login = 528
   - Client_FindPlayer = 20722
   - Client_QueryProfile = 3609
   - Client_QueryPawn = 8455
   - Client_QueryInventory = 42507
   - Client_QueryProgression = 40214
   - Client_Logout = 60160
   - Client_SwitchScene = 53343
   - Client_ChangeGold = 25343
   - Client_EquipItem = 53234
   - Client_GrantExperience = 26406
   - Client_ModifyHealth = 29243
   - Client_CastSkill = 30785
   - Client_ScenePlayerEnter = 2660
   - Client_ScenePlayerUpdate = 43756
   - Client_ScenePlayerLeave = 1143

六、序列化规则
1. 当前不是 protobuf，也不是 JSON
2. 当前是按字段声明顺序顺排的反射序列化
3. 第一版至少支持：
   - bool
   - uint16
   - uint32
   - uint64
   - int32
   - float
   - UTF-8 string

七、推荐实现结构
请拆成下面几个模块，不要把全部逻辑塞进一个类：
1. FMessionTcpClient
   - TCP 连接、发送、接收、长度前缀拆包
2. FMessionProtocolCodec
   - FunctionCall 请求响应包与 downlink 包编解码
   - 管理 CallId
   - 计算 FunctionId
3. FMessionReflectCodec
   - little-endian 基础类型和 UTF-8 string 编解码
   - 当前消息结构顺序编解码
4. UMessionGatewaySubsystem
   - Connect / Heartbeat / Login / Query / Write
   - 缓存 LoggedInPlayerId / SessionKey / Pending Calls
5. UMessionPlayerSyncSubsystem
   - 处理 ScenePlayerEnter / Update / Leave
   - 维护 RemotePlayers 映射

八、关键业务事实
1. Client_Heartbeat 当前是 Gateway 本地调用，不转发到 World
2. Client_Login 成功意味着玩家已进入当前 World 状态，而不只是拿到 SessionKey
3. 当前 validate.py 已覆盖登录、查询、写操作、场景同步、登出后重登和错误透传
4. ScenePlayerEnter/Update/Leave 已经是当前主线协议的一部分，不是占位设计
5. SessionKey 当前只需要缓存，不需要在后续请求里回传
6. TCP 断开后应视为连接态和登录态失效

九、最低交付标准
1. 能配置 Host / Port / PlayerId
2. 能建立 TCP 连接
3. 能发送 Heartbeat / Login / FindPlayer / QueryProfile
4. 能按 CallId 匹配请求响应
5. 能正确区分 downlink 与普通响应
6. 能处理 ScenePlayerEnter / Update / Leave
7. 日志中必须打印：
   - Connect success / fail
   - FunctionId / CallId
   - Login result: bSuccess / PlayerId / SessionKey / Error
   - Query result
   - Downlink result
   - Timeout / ParseError / BusinessError

十、完成后请提供
1. 你新增或修改的文件列表
2. 关键类职责说明
3. 如何在 UE 工程里触发一次登录和一次玩家同步联调
4. 一段示例日志，展示 Login + QueryProfile + ScenePlayerEnter 成功路径

参考文档：
- Docs/UE_LoginIntegrationGuide.md
- Docs/UE_ClientSkeletonDesign.md
- Docs/UE_PlayerSyncAgentGuide.md
- Docs/UE_HeartbeatIntegrationGuide.md

如果你要开始实现，请先输出文件规划和模块边界，再开始写代码。
```

## 推荐使用方式

如果你准备把任务转给 UE Agent，建议把本文档和上面 4 份协议文档一起给它。

这样 UE Agent 能同时看到：

- 当前协议事实
- 推荐模块边界
- 场景同步约束
- 心跳语义
