# 基础库说明

> 游戏/服务端共用类型与工具，集中在 Core 与 Common，新代码优先使用项目别名与封装，避免直接使用 STL 类型名（见 [.cursor/rules/project-cpp-style.mdc](../.cursor/rules/project-cpp-style.mdc)）。

## Core（Core/NetCore.h）

- **类型别名**：`uint8`～`uint64`、`int8`～`int64`、`float32`、`double64`；`FString`/`TString`、`TArray`/`TByteArray`、`TVector`、`TMap`、`TList`、`TQueue`、`TSet`、`TUnorderedMap`、`TUnorderedSet`、`TSharedPtr`/`TWeakPtr`/`TUniquePtr`、`TFunction`、`TOptional`、`TPair`、`TIfstream`/`TOfstream`。
- **智能指针**：`MakeShared<T>(...)` 统一构造 `TSharedPtr`。
- **字节序**：`HostToNetwork` / `NetworkToHost`（uint16/32/64）。
- **数学**：`SVector`（含 `Size`、`SizeSquared`、`Normalized()`、`Dot(V)`）、`Distance(A, B)`、`SRotator`、`STransform`；`Clamp(float, float, float)`、`Lerp(float, float, float)`、`Lerp(SVector, SVector, float)`。
- **时间**：`MTime::GetTimeSeconds()`、`SleepSeconds`/`SleepMilliseconds`。
- **错误返回**：`TResult<T, E>`、`TResult<void, E>`（`IsOk`/`IsErr`、`GetValue`/`GetError`）。
- **其他**：`MUniqueIdGenerator::Generate()`、`MNonCopyable`；C++20 下 `TSpan<T>`、`TSpanMutable<T>`。

## Common

- **Config（Common/Config.h）**：`MConfig::LoadFromFile`、`GetStr`/`GetInt`/`GetU16`/`GetU32`/`GetU64`/`GetBool`、`GetEnv`/`GetEnvInt`、`ApplyEnvOverrides`。
- **字符串（Common/StringUtils.h）**：`MString::ToString(int32/64, uint32/64, float, double)`（size_t 可转 uint64 后调用）、`MString::TrimInPlace`、`MString::TrimCopy`、`MString::Split(Str, char)`、`MString::Join(Parts, char)`。
- **日志**：`Common/Logger.h`、`Common/LogSink.h`；`LOG_DEBUG`/`LOG_INFO`/`LOG_WARN`/`LOG_ERROR`/`LOG_FATAL`，`MLogger::DefaultLogger()`、`LogStartupBanner`/`LogStarted`。
- **协议与消息**：`Common/MessageUtils.h`（`MMessageWriter`/`MMessageReader`）、`Common/ServerMessages.h`（各 `S*Message`、`ParsePayload`、`BuildPayload`、`SendTypedServerMessage`）。
- **服务器连接**：`Common/ServerConnection.h`（`MServerConnection`、`EServerMessageType` 等）。
- **命令行**：`Common/ParseArgs.h`（`MParseArgs::Parse`）。

## 网络层（Core）

- **Socket**：`Core/Socket.h`、`SocketPlatform.h`、`SocketAddress.h`、`SocketHandle.h`、`PacketCodec.h`；`MSocket::CreateListenSocket`、`MTcpConnection`、`MLengthPrefixedPacketCodec`。
- **轮询**：`Core/Poll.h`、`MSocketPoller`。

## 使用建议

1. 数值转字符串用 `MString::ToString(...)`，不直接写 `std::to_string`。按分隔符拆/拼用 `MString::Split` / `MString::Join`。
2. 需要“集合、无顺序、O(1) 查找”时用 `TUnorderedSet`；需要有序集合用 `TSet`。
3. 可能失败的操作返回 `TResult<T, FString>`，调用方检查 `IsOk()` 并处理 `GetError()`。
4. 配置读取统一用 `MConfig::Get*`，避免手写 `atoi`/`strtol`。
