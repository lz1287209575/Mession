#pragma once

// MemberRpcManifest.generated.h — 成员 RPC 注册表静态声明部分。
//
// 条目（GMemberRpcEntries）与 FindMemberRpcEntryByFunctionId definition 由
// MHeaderTool 在 Build/Generated/MemberRpcManifest.mgenerated.cpp 里 emit：
// 扫描 Type=ActorMember 类的 MFUNCTION(ServerCall),FunctionId 用
// ComputeStableReflectId(member 类名, 方法名)。框架成员分发器
// (DispatchFrameworkMemberCall, ActorMember.cpp)按 FunctionId 查表分发。

#include <cstddef>
#include <cstdint>

/** 成员 RPC 注册表条目:FunctionId → (成员类, 方法, 请求类型)。 */
struct SMemberRpcEntry {
    uint16_t    FunctionId;
    const char* MemberClass; // 成员类名(宿主成员表键)
    const char* MethodName;  // 成员方法名(反射 FindFunction)
    const char* RequestType; // 请求类型(反序列化取 PlayerId 用,可为空)
};

// 定义在生成的 MemberRpcManifest.mgenerated.cpp
const SMemberRpcEntry* FindMemberRpcEntryByFunctionId(uint16_t FunctionId);
const SMemberRpcEntry* FindMemberRpcEntry(size_t Index);
size_t                 GetMemberRpcEntryCount();
