/**
 * @file EReloadResult.h
 * @brief EReloadResult —— 脚本热重载结果枚举(经 MENUM 反射注册)。
 *
 * @detail NOTE: 失败路径实际走 TResult 的 Err 通道(错误码字符串),不返回本枚举值;
 *   当前代码里只会产生 Success。Partial / Failed 为 DualVM 细化预留,暂无产生路径。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

namespace mession::script {

    /**
     * @brief EReloadResult - 热重载结果。
     */
    enum class EReloadResult : uint8 {
        Success = 0, // 重载完成;当前唯一会被返回的值
        Partial = 1, // 预留:部分迁移成功
        Failed  = 2, // 预留;实际失败用 TResult::Err 表达
    };

} // namespace mession::script

MENUM(EReloadResult)