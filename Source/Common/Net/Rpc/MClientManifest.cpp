// MClientManifest.cpp — Manifest definitions
//
// 当前 PoC 阶段没有 MFUNCTION(Client/ClientCall)，GClientManifestEntries 为空。
// FindByFunctionId 始终返回 nullptr。
//
// 这个 .cpp 由 CMake 强制编译进 mession_common 库——保证 MClientManifest 符号
// 在所有 Service binary 里都能解析。

#include "Common/Net/Rpc/MClientManifest.generated.h"