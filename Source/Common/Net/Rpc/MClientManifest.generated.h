#pragma once

// MClientManifest.generated.h — 静态声明部分。
//
// 条目（GClientManifestEntries）与 FindByFunctionId definition 由 MHeaderTool
// 在 Build/Generated/MClientManifest.mgenerated.cpp 里 emit：当前 PoC 阶段业务侧
// 没有 MFUNCTION(Client/CallClient) 声明，生成器补收集"客户端可经 Gateway 调用的
// ServerCall"（如 MEchoService.Echo / EchoAwait），条目非空。整体 MClientManifest
// 类本身在 Source/Common/Net/Rpc/MClientManifest.cpp 里 hook（保证符号在
// mession_common 静态库里能找到）。

#include <cstdint>
#include <cstddef>

struct MClientManifest
{
    struct SEntry
    {
        uint16_t FunctionId;
        const char* OwnerType;
        const char* FunctionName;
        const char* ResponseTypeName;
        const char* TargetName;
        const char* ClientApiName;
    };

    static const SEntry* FindByFunctionId(uint16_t FunctionId);

    static const SEntry GClientManifestEntries[];
    static const size_t GClientManifestEntryCount;
};

