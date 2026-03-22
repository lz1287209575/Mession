#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FAppError
{
    MPROPERTY()
    MString Code;

    MPROPERTY()
    MString Message;

    static FAppError Make(MString InCode, MString InMessage = "")
    {
        FAppError Error;
        Error.Code = std::move(InCode);
        Error.Message = std::move(InMessage);
        return Error;
    }
};
