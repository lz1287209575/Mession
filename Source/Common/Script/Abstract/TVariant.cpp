/**
 * @file TVariant.cpp
 * @brief 实现 TVariant 的 4 个类型化取值函数:类型不符即返回 Err,不做隐式转换。
 */
#include "Common/Script/Abstract/TVariant.h"

namespace mession::script {

    /**
     * @brief AsBool - 取 bool 值。
     * @return 类型为 Bool 时 Ok(值);否则 Err("type mismatch")
     *
     * @detail 不做 0/1 或真值转换,必须类型完全匹配。
     */
    TResult<bool> TVariant::AsBool() const {
        if (Type != EVariantType::Bool) {
            return TResult<bool>::Err(MString("TVariant type mismatch: not Bool"));
        }
        return TResult<bool>::Ok(BoolVal);
    }

    /**
     * @brief AsInt - 取 int64 值。
     * @return 类型为 Int 时 Ok(值);否则 Err("type mismatch")
     */
    TResult<int64> TVariant::AsInt() const {
        if (Type != EVariantType::Int) {
            return TResult<int64>::Err(MString("TVariant type mismatch: not Int"));
        }
        return TResult<int64>::Ok(IntVal);
    }

    /**
     * @brief AsDouble - 取 double 值。
     * @return 类型为 Double 时 Ok(值);否则 Err("type mismatch")
     */
    TResult<double> TVariant::AsDouble() const {
        if (Type != EVariantType::Double) {
            return TResult<double>::Err(MString("TVariant type mismatch: not Double"));
        }
        return TResult<double>::Ok(DoubleVal);
    }

    /**
     * @brief AsString - 取 MString 值(按值拷贝)。
     * @return 类型为 String 时 Ok(值);否则 Err("type mismatch")
     */
    TResult<MString> TVariant::AsString() const {
        if (Type != EVariantType::String) {
            return TResult<MString>::Err(MString("TVariant type mismatch: not String"));
        }
        return TResult<MString>::Ok(StringVal);
    }

} // namespace mession::script