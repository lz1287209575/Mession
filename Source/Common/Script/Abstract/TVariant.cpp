#include "Common/Script/Abstract/TVariant.h"

namespace mession::script {

TResult<bool> TVariant::AsBool() const
{
    if (Type != EVariantType::Bool)
    {
        return TResult<bool>::Err(MString("TVariant type mismatch: not Bool"));
    }
    return TResult<bool>::Ok(BoolVal);
}

TResult<int64> TVariant::AsInt() const
{
    if (Type != EVariantType::Int)
    {
        return TResult<int64>::Err(MString("TVariant type mismatch: not Int"));
    }
    return TResult<int64>::Ok(IntVal);
}

TResult<double> TVariant::AsDouble() const
{
    if (Type != EVariantType::Double)
    {
        return TResult<double>::Err(MString("TVariant type mismatch: not Double"));
    }
    return TResult<double>::Ok(DoubleVal);
}

TResult<MString> TVariant::AsString() const
{
    if (Type != EVariantType::String)
    {
        return TResult<MString>::Err(MString("TVariant type mismatch: not String"));
    }
    return TResult<MString>::Ok(StringVal);
}

} // namespace mession::script