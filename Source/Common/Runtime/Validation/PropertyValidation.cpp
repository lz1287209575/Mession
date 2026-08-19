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
    if (Value < Min || Value > Max) {
        return TResult<void, FAppError>::Err(FAppError::Make("validation.out_of_range", MString(PropertyName) + " must be between " + std::to_string(Min) + " and " + std::to_string(Max)));
    }
    return TResult<void, FAppError>::Ok();
}
