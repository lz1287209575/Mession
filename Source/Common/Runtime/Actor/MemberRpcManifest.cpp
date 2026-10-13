/**
 * @file MemberRpcManifest.cpp
 * @brief 成员 RPC 注册表的进程内定义文件(表体由 MHeaderTool 生成)。
 *
 * 本文件只放「必须由手写 .cpp 提供」的那部分:表体 GMemberRpcEntries 与
 * FindMemberRpcEntryByFunctionId 的定义都在
 * Build/Generated/MemberRpcManifest.mgenerated.cpp 里由 MHeaderTool 生成 ——
 * 它扫描 Type=ActorMember 类上的 MFUNCTION(ServerCall),FunctionId 用
 * ComputeStableReflectId(member 类名, 方法名) 计算。
 *
 * 本 .cpp 由 CMake 强制编进 mession_common 库,以保证成员框架符号
 * (FindMemberRpcEntryByFunctionId 等)在所有 Service binary 里都能解析。
 */

#include "Common/Runtime/Actor/MemberRpcManifest.generated.h"
