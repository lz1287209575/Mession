// MClientManifest.cpp — Manifest definitions
//
// 条目（GClientManifestEntries）与 FindByFunctionId definition 由 MHeaderTool 在
// Build/Generated/MClientManifest.mgenerated.cpp 里 emit：当前 PoC 阶段业务侧
// 没有 MFUNCTION(Client/CallClient) 声明，生成器补收集"客户端可经 Gateway 调用的
// ServerCall"（如 MEchoService.Echo / EchoAwait），条目非空。
//
// 这个 .cpp 由 CMake 强制编译进 mession_common 库——保证 MClientManifest 符号
// 在所有 Service binary 里都能解析。

#include "Common/Net/Rpc/MClientManifest.generated.h"