#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"

namespace mession::script {

    enum class EVariantType : uint8 {
        Null   = 0,
        Bool   = 1,
        Int    = 2,
        Double = 3,
        String = 4,
    };

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

        EVariantType GetType() const {
            return Type;
        }

        TResult<bool>    AsBool() const;
        TResult<int64>   AsInt() const;
        TResult<double>  AsDouble() const;
        TResult<MString> AsString() const;

        private:
        EVariantType Type      = EVariantType::Null;
        bool         BoolVal   = false;
        int64        IntVal    = 0;
        double       DoubleVal = 0.0;
        MString      StringVal;
    };

} // namespace mession::script