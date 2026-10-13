/**
 * @file AppMessages.h
 * @brief 应用层通用错误载体 FAppError。
 *
 * 只承载两个字符串:稳定的错误码 + 面向人的说明。错误码由各业务域自己定义
 * (例如 Lua 相关错误的常量在 Common/Script/Abstract/ScriptErrorCodes.h),
 * 这里不做枚举约束,以免跨进程 / 跨语言传递时还要同步一份枚举表。
 *
 * @note 全仓约定:异步结果类型写作 `TResult<T, FAppError>`,失败分支通过
 *       GetError() 取到本结构,读 Message 拿说明(没有 what() 成员)。
 */
#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief FAppError - 应用层错误(错误码 + 说明)。
 */
MSTRUCT()
struct FAppError {
    /// 错误码;非空字符串,取值由业务域约定
    MPROPERTY()
    MString Code;

    /// 错误说明;可为空(调用方只关心 Code 时)
    MPROPERTY()
    MString Message;

    /**
     * @brief Make - 便捷构造。
     * @param InCode    错误码
     * @param InMessage 错误说明,默认空串
     * @return 构造好的 FAppError(两个入参都移动进去,不额外拷贝)
     */
    static FAppError Make(MString InCode, MString InMessage = "") {
        FAppError Error;
        Error.Code    = std::move(InCode);
        Error.Message = std::move(InMessage);
        return Error;
    }
};
