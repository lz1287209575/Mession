/**
 * @file PropertyValidation.cpp
 * @brief PropertyValidation.h 中三个校验谓词的实现。
 *
 * @detail 只做判断与错误信息拼装,不写日志、不上报:是否记录由调用方决定。
 * 三个错误码字面量("validation.not_zero" / "validation.not_empty" /
 * "validation.out_of_range")与头文件注释保持一致,属于对外契约。
 */
#include "Common/Runtime/Validation/PropertyValidation.h"

FValidationResult ValidateNotZero(uint64 Value, const char* PropertyName) {
    if (Value == 0) {
        return TResult<void, FAppError>::Err(FAppError::Make("validation.not_zero", MString(PropertyName) + " must not be zero"));
    }
    return TResult<void, FAppError>::Ok();
}

FValidationResult ValidateNotEmpty(const MString& Value, const char* PropertyName) {
    if (Value.empty()) {
        return TResult<void, FAppError>::Err(FAppError::Make("validation.not_empty", MString(PropertyName) + " must not be empty"));
    }
    return TResult<void, FAppError>::Ok();
}

FValidationResult ValidateRange(int32 Value, int32 Min, int32 Max, const char* PropertyName) {
    // 区间是闭区间;Min > Max 时不特判,统一落到 out_of_range
    if (Value < Min || Value > Max) {
        return TResult<void, FAppError>::Err(FAppError::Make("validation.out_of_range", MString(PropertyName) + " must be between " + std::to_string(Min) + " and " + std::to_string(Max)));
    }
    return TResult<void, FAppError>::Ok();
}
