/**
 * @file PropertyValidation.h
 * @brief 反射属性写入前的通用校验谓词:非零 / 非空 / 闭区间。
 *
 * @detail 统一返回 FValidationResult = TResult<void, FAppError>:
 *   成功 Ok();失败带稳定错误码(如 "validation.not_zero")与英文可读信息。
 * 错误码是上层做本地化/断言/客户端提示的稳定标识,字面量属于对外契约,不要改动。
 * PropertyName 会被用于拼装错误信息,必须是非空 C 字符串(内部据此构造 MString,
 * 传 nullptr 是未定义行为)。
 */
#pragma once

#include "Protocol/Messages/Common/AppMessages.h"

// 校验结果统一类型:成功无值,失败携带 FAppError(错误码 + 信息)
using FValidationResult = TResult<void, FAppError>;

/**
 * @brief ValidateNotZero - 要求 64 位整数非 0(常用于 Id / 句柄类字段)。
 * @param Value        待校验值
 * @param PropertyName 字段名(非空),用于错误信息
 * @return Ok();或 Err("validation.not_zero", "<PropertyName> must not be zero")
 */
FValidationResult ValidateNotZero(uint64 Value, const char* PropertyName);

/**
 * @brief ValidateNotEmpty - 要求字符串非空。
 * @param Value        待校验字符串
 * @param PropertyName 字段名(非空),用于错误信息
 * @return Ok();或 Err("validation.not_empty", "<PropertyName> must not be empty")
 *
 * @detail 判定条件是 empty():全空白字符串("   ")算通过,不做 trim。
 */
FValidationResult ValidateNotEmpty(const MString& Value, const char* PropertyName);

/**
 * @brief ValidateRange - 要求 32 位整数落在闭区间 [Min, Max] 内。
 * @param Value        待校验值
 * @param Min          下界(含)
 * @param Max          上界(含)
 * @param PropertyName 字段名(非空),用于错误信息
 * @return Ok();或 Err("validation.out_of_range", "<PropertyName> must be between <Min> and <Max>")
 *
 * @detail Min > Max 时不单独报错,而是任何 Value 都不满足,统一走 out_of_range。
 */
FValidationResult ValidateRange(int32 Value, int32 Min, int32 Max, const char* PropertyName);
