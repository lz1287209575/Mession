# 协议与字节序说明

## 当前约定

- **客户端↔Gateway 包长与业务载荷**：当前保持**主机字节序**（现网等价于小端）；`Scripts/validate.py`、`Scripts/test_client.py` 仍按小端读写。
- **跨服消息（ServerMessages）**：
  - 已切到**网络字节序（大端）**的首批消息：`SPlayerLoginResponseMessage`、`SSessionValidateRequestMessage`、`SPlayerLogoutMessage`、`SPlayerClientSyncMessage` 的整数头字段。
  - 仍保持**主机字节序**的消息：其余 `ServerMessages` 结构，以及所有字符串长度字段。
- **协议验证脚本**：`Scripts/verify_protocol.py` 现已按上述首批网络序消息执行 round-trip / fixed-blob 校验。

## 为什么先这样切

- 这批消息都是**跨服主链路上的关键整数载荷**，字段简单，最适合作为第一批网络序迁移样本。
- 它们覆盖了登录结果、Session 校验、登出清理和按 `PlayerId` 路由的客户端同步，价值高，但不会直接改动客户端协议。
- 暂时不动 `PacketCodec` 的长度前缀和客户端协议，能把风险控制在服务器内部消息体，避免一次改动影响所有测试脚本和收发链路。
- 暂时不动字符串长度字段，是因为当前首批目标消息都不依赖字符串；先把“纯整数消息”迁干净，后续再统一字符串长度和其他结构会更稳。

## 后续建议

- 下一批优先考虑 Router 相关的纯整数或“少量字符串 + 整数”的消息，例如 `SRouteQueryMessage`、`SSessionValidateResponseMessage`、`SHeartbeatMessage`。
- 若要继续推进到客户端或连接层，再单独评估 `PacketCodec` 长度前缀是否切到网络序，并同步更新所有 Python 脚本和调试工具。
- 当字符串参与迁移时，统一使用 `WriteStringBE` / `ReadStringBE`，避免“字段是 BE、长度还是 LE”的混用。
