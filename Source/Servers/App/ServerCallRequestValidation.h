#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/StringUtils.h"
#include "Protocol/Messages/Common/AppMessages.h"

#include <cerrno>
#include <cstdlib>

namespace MServerCallRequestValidation
{
namespace MDetail
{
inline MString ResolveValidationContext(const MClass* RequestClass, const MProperty* Property)
{
    if (Property)
    {
        if (const MString* ErrorMessage = Property->FindMetadata("ErrorMessage"))
        {
            return *ErrorMessage;
        }
        if (const MString* ErrorContext = Property->FindMetadata("ErrorContext"))
        {
            return *ErrorContext;
        }
    }

    return RequestClass ? RequestClass->GetName() : "";
}

inline MString ResolveValidationCode(const MProperty* Property, const char* FallbackCode)
{
    if (Property)
    {
        if (const MString* ErrorCode = Property->FindMetadata("ErrorCode"))
        {
            return *ErrorCode;
        }
    }

    return FallbackCode ? FallbackCode : "request_validation_failed";
}

inline TOptional<long double> TryParseNumericMeta(const MString& Value)
{
    if (Value.empty())
    {
        return std::nullopt;
    }

    char* End = nullptr;
    errno = 0;
    const long double Parsed = std::strtold(Value.c_str(), &End);
    if (End == Value.c_str() || (End && *End != '\0') || errno != 0)
    {
        return std::nullopt;
    }

    return Parsed;
}

inline TOptional<long double> ReadNumericPropertyValue(const MProperty* Property, const void* Object)
{
    if (!Property || !Object)
    {
        return std::nullopt;
    }

    switch (Property->Type)
    {
    case EPropertyType::Int8:
        return static_cast<long double>(*Property->GetValuePtr<int8>(Object));
    case EPropertyType::Int16:
        return static_cast<long double>(*Property->GetValuePtr<int16>(Object));
    case EPropertyType::Int32:
        return static_cast<long double>(*Property->GetValuePtr<int32>(Object));
    case EPropertyType::Int64:
        return static_cast<long double>(*Property->GetValuePtr<int64>(Object));
    case EPropertyType::UInt8:
        return static_cast<long double>(*Property->GetValuePtr<uint8>(Object));
    case EPropertyType::UInt16:
        return static_cast<long double>(*Property->GetValuePtr<uint16>(Object));
    case EPropertyType::UInt32:
        return static_cast<long double>(*Property->GetValuePtr<uint32>(Object));
    case EPropertyType::UInt64:
        return static_cast<long double>(*Property->GetValuePtr<uint64>(Object));
    case EPropertyType::Float:
        return static_cast<long double>(*Property->GetValuePtr<float>(Object));
    case EPropertyType::Double:
        return static_cast<long double>(*Property->GetValuePtr<double>(Object));
    default:
        return std::nullopt;
    }
}

inline bool IsZeroValue(const MProperty* Property, const void* Object)
{
    const TOptional<long double> NumericValue = ReadNumericPropertyValue(Property, Object);
    return NumericValue.has_value() && *NumericValue == 0.0L;
}

inline bool IsEmptyValue(const MProperty* Property, const void* Object)
{
    if (!Property || !Object)
    {
        return false;
    }

    switch (Property->Type)
    {
    case EPropertyType::String:
    case EPropertyType::Name:
    {
        const MString* Value = Property->GetValuePtr<MString>(Object);
        return !Value || Value->empty();
    }
    default:
        return false;
    }
}

inline TOptional<FAppError> ValidatePropertyMetadata(
    const MClass* RequestClass,
    const MProperty* Property,
    const void* RequestObject)
{
    if (!Property || !RequestObject)
    {
        return std::nullopt;
    }

    const MString Context = ResolveValidationContext(RequestClass, Property);

    if (Property->HasMetadata("Required"))
    {
        if (IsZeroValue(Property, RequestObject) || IsEmptyValue(Property, RequestObject))
        {
            return FAppError::Make(ResolveValidationCode(Property, "request_field_required"), Context);
        }
    }

    if (Property->HasMetadata("NonZero") && IsZeroValue(Property, RequestObject))
    {
        return FAppError::Make(ResolveValidationCode(Property, "request_field_required"), Context);
    }

    if (Property->HasMetadata("NonEmpty") && IsEmptyValue(Property, RequestObject))
    {
        return FAppError::Make(ResolveValidationCode(Property, "request_field_required"), Context);
    }

    if (const MString* MinValue = Property->FindMetadata("Min"))
    {
        if (const TOptional<long double> Limit = TryParseNumericMeta(*MinValue))
        {
            if (const TOptional<long double> Current = ReadNumericPropertyValue(Property, RequestObject);
                Current.has_value() && *Current < *Limit)
            {
                return FAppError::Make(ResolveValidationCode(Property, "request_field_below_min"), Context);
            }
        }
    }

    if (const MString* MaxValue = Property->FindMetadata("Max"))
    {
        if (const TOptional<long double> Limit = TryParseNumericMeta(*MaxValue))
        {
            if (const TOptional<long double> Current = ReadNumericPropertyValue(Property, RequestObject);
                Current.has_value() && *Current > *Limit)
            {
                return FAppError::Make(ResolveValidationCode(Property, "request_field_above_max"), Context);
            }
        }
    }

    return std::nullopt;
}

template<typename TRequest>
TOptional<FAppError> ValidateRequestMetadata(const TRequest& Request)
{
    MClass* RequestClass = MObject::FindStruct(std::type_index(typeid(std::decay_t<TRequest>)));
    if (!RequestClass)
    {
        return std::nullopt;
    }

    for (const MProperty* Property : RequestClass->GetProperties())
    {
        if (const TOptional<FAppError> Error = ValidatePropertyMetadata(RequestClass, Property, &Request);
            Error.has_value())
        {
            return Error;
        }
    }

    return std::nullopt;
}
} // namespace MDetail

template<typename TRequest>
struct TRequestValidator
{
    static TOptional<FAppError> Validate(const TRequest&)
    {
        return std::nullopt;
    }
};

template<typename TRequest>
TOptional<FAppError> ValidateRequest(const TRequest& Request)
{
    if (const TOptional<FAppError> MetadataError = MDetail::ValidateRequestMetadata(Request);
        MetadataError.has_value())
    {
        return MetadataError;
    }

    return TRequestValidator<std::decay_t<TRequest>>::Validate(Request);
}

inline TOptional<FAppError> RequireNonZero(uint64 Value, const char* Code, const char* Message = "")
{
    if (Value == 0)
    {
        return FAppError::Make(Code ? Code : "request_field_required", Message ? Message : "");
    }

    return std::nullopt;
}

inline TOptional<FAppError> RequireNonZero(uint32 Value, const char* Code, const char* Message = "")
{
    if (Value == 0)
    {
        return FAppError::Make(Code ? Code : "request_field_required", Message ? Message : "");
    }

    return std::nullopt;
}
}
