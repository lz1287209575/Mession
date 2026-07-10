#pragma once

// MClientManifest.generated.h — 静态声明部分。
//
// 当前 PoC 阶段：MHeaderTool 在 Build/Generated/MClientManifest.mgenerated.cpp
// 里 emit 出空 entries 表 + FindByFunctionId definition。整体 MClientManifest
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

