// MemberRpcManifest.cpp — 成员 RPC 注册表 definitions
//
// 条目（GMemberRpcEntries）与 FindMemberRpcEntryByFunctionId definition 由
// MHeaderTool 在 Build/Generated/MemberRpcManifest.mgenerated.cpp 里 emit：
// 扫描 Type=ActorMember 类的 MFUNCTION(ServerCall),FunctionId 用
// ComputeStableReflectId(member 类名, 方法名)。
//
// 这个 .cpp 由 CMake 强制编译进 mession_common 库——保证成员框架符号
// (FindMemberRpcEntryByFunctionId 等)在所有 Service binary 里都能解析。

#include "Common/Runtime/Actor/MemberRpcManifest.generated.h"
