#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Common/ObjectStateMessages.h"

MSTRUCT()
struct FMgoLoadPlayerRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FMgoLoadPlayerResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    TVector<FObjectPersistenceRecord> Records;
};

MSTRUCT()
struct FMgoSavePlayerRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    TVector<FObjectPersistenceRecord> Records;
};

MSTRUCT()
struct FMgoSavePlayerResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;
};
