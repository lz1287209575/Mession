# UE 登录接入 Agent Prompt

本文档提供一份可以直接发给 UE 侧 Agent 的执行 Prompt。

适用日期：`2026-03-31`

建议配合阅读：

- `Docs/UE_LoginIntegrationGuide.md`

## 可直接复制的 Prompt

```text
你现在要在 Unreal Engine 工程里实现一个最小可运行的 Mession Gateway 登录客户端。

目标不是做完整游戏客户端，而是先把“连接 Gateway -> 登录 -> 登录后做一次状态确认”的链路跑通。

请严格按照下面约束实现：

一、协议与网络边界
1. 只直连 TCP Gateway：127.0.0.1:8001
2. 只实现客户端统一消息类型 MT_FunctionCall = 13
3. 所有 TCP 包都使用 length-prefixed framing：
   - uint32 PacketLength
   - PacketBody bytes
4. 当前协议必须按 little-endian 处理
5. 当前不要实现 Web 登录、账号密码、JWT、心跳、重连、复制、战斗、聊天

二、这次必须支持的客户端 API
1. Client_Login
   - FunctionId = 528
   - Request payload = uint64 PlayerId
   - Response = bool bSuccess + uint64 PlayerId + uint32 SessionKey + UTF-8 string Error
2. Client_FindPlayer
   - FunctionId = 20722
   - Request payload = uint64 PlayerId
   - Response = bool bFound + uint64 PlayerId + uint64 GatewayConnectionId + uint32 SceneId + UTF-8 string Error
3. 可选加分：Client_QueryProfile
   - FunctionId = 3609

三、MT_FunctionCall 包体格式
1. uint8 MsgType = 13
2. uint16 FunctionId
3. uint64 CallId
4. uint32 PayloadSize
5. Payload bytes

四、最小功能要求
1. 可配置 Host / Port / PlayerId
2. 能建立 TCP 连接
3. 能发送 Client_Login
4. 能按 CallId 匹配并解析响应
5. 登录成功后自动发送 Client_FindPlayer
6. UE 日志里必须打印：
   - Connect success / fail
   - Sent login callId
   - Login result: bSuccess / PlayerId / SessionKey / Error
   - FindPlayer result: bFound / GatewayConnectionId / SceneId / Error

五、推荐实现结构
请拆成下面几个模块，不要把全部逻辑塞进一个类：
1. FMessionTcpClient
   - 负责 TCP 连接、发送、接收、拆 length prefix
2. FMessionByteReader / FMessionByteWriter
   - 负责 little-endian 基础类型和 UTF-8 string 编解码
3. FMessionGatewayProtocol
   - 负责封装 MT_FunctionCall
   - 管理 NextCallId
   - 根据 CallId 分发响应
4. UMessionLoginSubsystem 或等价对象
   - 对外提供 Connect / Login / FindPlayer

六、最低交付标准
1. 能在 PIE 或 GameInstance 启动后手工触发 Login(PlayerId)
2. 登录成功后自动触发 FindPlayer(PlayerId)
3. 能处理以下失败情况并打印日志：
   - 连接失败
   - 超时
   - 包格式错误
   - 业务错误码

七、实现时必须遵守
1. 第一版不要做“通用反射系统”
2. 第一版只手写当前需要的结构体编解码
3. 不要引入额外第三方网络库
4. 优先用 UE 自带 Socket / Sockets / Networking 能力
5. 代码结构要方便后续扩展更多 Client_* 请求

八、验收方式
完成后请提供：
1. 你新增/修改的文件列表
2. 关键类职责说明
3. 如何在 UE 工程里触发一次登录
4. 一段示例日志，展示 Login + FindPlayer 成功路径

补充事实：
1. 当前服务端已经在 2026-03-31 验证过 python3 Scripts/validate.py --build-dir Build --no-build 可通过
2. SessionKey 当前只需要缓存，不需要带回后续请求
3. 如果 TCP 断开，应认为登录态失效，需要重新登录

参考文档：
- Docs/UE_LoginIntegrationGuide.md

如果你要开始实现，请先输出你的文件规划，再开始写代码。
```

## 推荐使用方式

如果你准备把任务转给 UE Agent，建议附带下面两项上下文一起给它：

1. `Docs/UE_LoginIntegrationGuide.md`
2. 本文档里的完整 Prompt

这样 UE Agent 既有协议事实，也有明确的交付边界。
