#pragma once

#include "Protocol/Messages/Common/AppMessages.h"

using FValidationResult = TResult<void, FAppError>;

FValidationResult ValidateNotZero(uint64 Value, const char* PropertyName);
FValidationResult ValidateNotEmpty(const MString& Value, const char* PropertyName);
FValidationResult ValidateRange(int32 Value, int32 Min, int32 Max, const char* PropertyName);
