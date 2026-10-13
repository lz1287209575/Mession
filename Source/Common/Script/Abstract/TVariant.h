/**
 * @file TVariant.h
 * @brief TVariant —— 脚本与 C++ 之间传递参数/返回值的小型 tagged union。
 *
 * @detail 只承载 5 种标量(Null/Bool/Int/Double/String),不支持嵌套容器;
 *   取值一律走 AsXxx(),类型不匹配返回 Err 而非抛异常。取值为值语义。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"

namespace mession::script {

    /**
     * @brief EVariantType - TVariant 的当前承载类型。
     */
    enum class EVariantType : uint8 {
        Null   = 0,
        Bool   = 1,
        Int    = 2, // int64
        Double = 3, // double
        String = 4, // MString
    };

    /**
     * @brief TVariant - 单值 tagged union,默认构造为 Null。
     *
     * @detail 各 MakeXxx 静态工厂按值构造(std::move 入参);未承载的联合成员保持
     *   默认值,不参与比较。
     */
    class TVariant {
        public:
        TVariant() = default;

        static TVariant MakeNull() {
            TVariant V;
            V.Type = EVariantType::Null;
            return V;
        }

        static TVariant MakeBool(bool Value) {
            TVariant V;
            V.Type    = EVariantType::Bool;
            V.BoolVal = Value;
            return V;
        }

        static TVariant MakeInt(int64 Value) {
            TVariant V;
            V.Type   = EVariantType::Int;
            V.IntVal = Value;
            return V;
        }

        static TVariant MakeDouble(double Value) {
            TVariant V;
            V.Type      = EVariantType::Double;
            V.DoubleVal = Value;
            return V;
        }

        static TVariant MakeString(MString Value) {
            TVariant V;
            V.Type      = EVariantType::String;
            V.StringVal = std::move(Value);
            return V;
        }

        /** @brief GetType - 当前承载类型 */
        EVariantType GetType() const {
            return Type;
        }

        /** @brief AsBool - 按 bool 取值;类型不是 Bool 时返回 Err */
        TResult<bool> AsBool() const;
        /** @brief AsInt - 按 int64 取值;类型不是 Int 时返回 Err */
        TResult<int64> AsInt() const;
        /** @brief AsDouble - 按 double 取值;类型不是 Double 时返回 Err */
        TResult<double> AsDouble() const;
        /** @brief AsString - 按 MString 取值(拷贝);类型不是 String 时返回 Err */
        TResult<MString> AsString() const;

        private:
        EVariantType Type      = EVariantType::Null;
        bool         BoolVal   = false;
        int64        IntVal    = 0;
        double       DoubleVal = 0.0;
        MString      StringVal;
    };

} // namespace mession::script