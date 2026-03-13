# 协议与字节序说明

## 当前约定

- **跨服消息（ServerMessages）**：`MMessageWriter` / `MMessageReader` 使用 `MessageUtils.h` 的 `AppendValue` / `ReadValue`，即**主机字节序**。当前部署多为同构环境（同机或同架构），暂未统一为网络字节序。
- **客户端↔Gateway**：与脚本/验证客户端约定一致，当前也为**主机字节序**（小端）；`scripts/validate.py`、`scripts/verify_protocol.py` 按小端读写。

## 后续可选

- 若需跨平台/跨端（如移动端、不同 CPU 架构）客户端，建议在协议层统一为**网络字节序（大端）**：在 `MessageUtils.h` 中为整数/浮点提供 `AppendValueBE` / `ReadValueBE` 的封装，并在对外协议（客户端协议或部分跨服字段）中改用 BE 接口；或单独维护一份「网络序」的 Serialize/Deserialize 路径。
- 评估后可在本仓库中标注：哪些消息类型已固定为网络序、哪些仍为主机序，避免混用导致解析错误。
