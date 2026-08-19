#pragma once

// MClientDownlinkManifest — 服务端→客户端下行通知清单（静态声明部分）。
//
// 条目（GDownlinkEntries）与 FindByFunctionId definition 由 MHeaderTool 在
// Build/Generated/MClientDownlinkManifest.mgenerated.cpp 里 emit：收集所有
// MFUNCTION(CallClient) 声明，为每个分配下行 FunctionId（ClientApi 或
// (OwnerType, FunctionName) 稳定 id）。服务端业务经此表把下行通知路由到
// Gateway（PushClientDownlink）再发给客户端连接。
// 整体类本身在 Source/Common/Net/Rpc/MClientDownlinkManifest.cpp 里 hook
// （保证符号在 mession_common 静态库里能找到）。

#include <cstddef>
#include <cstdint>

struct MClientDownlinkManifest {
    struct SDownlinkEntry {
        uint16_t    FunctionId;
        const char* OwnerType;
        const char* FunctionName;
        const char* ResponseTypeName;
    };

    static const SDownlinkEntry* FindByFunctionId(uint16_t FunctionId);

    static const SDownlinkEntry GDownlinkEntries[];
    static const size_t GDownlinkEntryCount;
};
