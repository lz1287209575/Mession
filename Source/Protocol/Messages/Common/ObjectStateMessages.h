#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FObjectPersistenceRecord
{
    MPROPERTY()
    MString ObjectPath;

    MPROPERTY()
    MString ClassName;

    MPROPERTY()
    TByteArray SnapshotData;
};
