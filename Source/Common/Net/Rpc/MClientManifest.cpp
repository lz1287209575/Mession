// MClientManifest.cpp — Manifest definitions
//
// 条目（GClientManifestEntries）与 FindByFunctionId definition 由 MHeaderTool 在
// Build/Generated/MClientManifest.mgenerated.cpp 里 emit：收集"客户端可经 Gateway
// 调用的 ServerCall"（如 MEchoService.Echo / EchoAwait / TriggerNotify /
// RegisterDynamicActor），条目非空；MFUNCTION(CallClient) 走下行
// MClientDownlinkManifest。
//
// 这个 .cpp 由 CMake 强制编译进 mession_common 库——保证 MClientManifest 符号
// 在所有 Service binary 里都能解析。

#include "Common/Net/Rpc/MClientManifest.generated.h"