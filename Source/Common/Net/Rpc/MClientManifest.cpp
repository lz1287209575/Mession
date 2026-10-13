/**
 * @file MClientManifest.cpp
 * @brief 空 hook —— 只为把 MClientManifest 的生成符号拉进 mession_common。
 *
 * @detail 条目 GClientManifestEntries 与 FindByFunctionId 的定义由 MHeaderTool 生成到
 *   Build/Generated/MClientManifest.mgenerated.cpp:它收集"客户端可经 Gateway 调用的
 *   ServerCall"(如 MEchoService 的 Echo / EchoAwait / TriggerNotify / RegisterDynamicActor),
 *   因此该表非空;MFUNCTION(CallClient) 声明则走 MClientDownlinkManifest。
 *
 *   本文件由 CMake 强制编译进 mession_common 静态库,保证 MClientManifest 符号在
 *   所有 Service binary 里都能解析到(否则静态库中无人引用的符号会被丢弃)。
 */

#include "Common/Net/Rpc/MClientManifest.generated.h"