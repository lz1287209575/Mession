/**
 * @file MClientDownlinkManifest.cpp
 * @brief 空 hook —— 只为把 MClientDownlinkManifest 的生成符号拉进 mession_common。
 *
 * @detail 本文件不定义任何内容:条目 GDownlinkEntries 与 FindByFunctionId 的定义都由
 *   MHeaderTool 生成到 Build/Generated/MClientDownlinkManifest.mgenerated.cpp。
 *   这里由 CMake 强制编译进 mession_common 静态库,避免静态库中符号无人引用而被丢弃,
 *   导致各 Service binary 链接时找不到 GDownlinkEntries / FindByFunctionId。
 */

#include "Common/Net/Rpc/MClientDownlinkManifest.h"
