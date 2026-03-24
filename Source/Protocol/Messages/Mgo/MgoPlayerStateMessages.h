#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FMgoLoadPlayerRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FMgoPersistenceRecord
{
    MPROPERTY()
    MString ObjectPath;

    MPROPERTY()
    MString ClassName;

    MPROPERTY()
    TByteArray SnapshotData;
};

MSTRUCT()
struct FMgoLoadPlayerResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    TVector<FMgoPersistenceRecord> Records;
};

MSTRUCT()
struct FMgoSavePlayerRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    TVector<FMgoPersistenceRecord> Records;
};

MSTRUCT()
struct FMgoSavePlayerResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;
};
