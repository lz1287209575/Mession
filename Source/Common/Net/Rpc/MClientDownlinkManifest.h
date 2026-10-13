/**
 * @file MClientDownlinkManifest.h
 * @brief MClientDownlinkManifest - 服务端→客户端**下行通知**清单(静态声明部分)。
 *
 * @detail 条目 GDownlinkEntries[] 与 FindByFunctionId 的定义由 MHeaderTool 生成到
 *   Build/Generated/MClientDownlinkManifest.mgenerated.cpp:它扫描所有
 *   MFUNCTION(CallClient) 声明,为每个函数分配稳定的下行 FunctionId
 *   (取 ClientApi 名,或 (OwnerType, FunctionName) 派生)。
 *
 *   服务端业务凭此表把下行通知交给 Gateway(PushClientDownlink),再由 Gateway 投递到
 *   客户端连接 —— 因此 FunctionId 一旦发布即属于线上兼容面,不可随意改动。
 *
 *   本头文件只声明;MClientDownlinkManifest.cpp 是一个空 hook,存在的唯一目的是让
 *   GDownlinkEntries / FindByFunctionId 这两个符号被 CMake 编进 mession_common 静态库,
 *   否则生成物不会被链接进 Service binary。
 */

#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief MClientDownlinkManifest - 下行通知 FunctionId → 声明信息的只读查询表。
 */
struct MClientDownlinkManifest {
    /// 一条下行通知的静态描述;字符串字面量由生成器产出,生命周期是整个进程。
    struct SDownlinkEntry {
        uint16_t    FunctionId;       ///< 下行通知的稳定 FunctionId(0 保留,不会出现在表里)
        const char* OwnerType;        ///< 声明该 MFUNCTION(CallClient) 的类名
        const char* FunctionName;     ///< 函数名
        const char* ResponseTypeName; ///< 响应类型名(无响应时为空)
    };

    /**
     * @brief FindByFunctionId - 按 FunctionId 查表。
     * @param FunctionId 下行通知 id
     * @return 命中返回条目指针;未命中返回 nullptr
     *
     * @detail 线性扫描生成表,表规模 = 全部 MFUNCTION(CallClient) 声明数,调用方可自行缓存结果。
     */
    static const SDownlinkEntry* FindByFunctionId(uint16_t FunctionId);

    static const SDownlinkEntry GDownlinkEntries[];  ///< 生成器产出的条目数组
    static const size_t         GDownlinkEntryCount; ///< 条目数
};
