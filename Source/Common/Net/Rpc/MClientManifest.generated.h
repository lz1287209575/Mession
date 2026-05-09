#pragma once
// Minimal stub for MClientManifest - generated code that was previously in a separate generator

#include "Common/Runtime/Reflect/Reflection.h"
#include <cstdint>

struct MClientManifest
{
    struct SEntry
    {
        uint16 FunctionId;
        const char* OwnerType;
        const char* FunctionName;
        const char* ResponseTypeName;
        const char* TargetName;
    };

    static const SEntry* FindByFunctionId(uint16 FunctionId)
    {
        return nullptr;
    }
};
