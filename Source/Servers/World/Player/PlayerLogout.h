#pragma once
#include "Common/Runtime/Persistence/PersistenceSubsystem.h"
#include "Protocol/Messages/World/PlayerLifecycleMessages.h"

template<typename TResponse>
TResponse BuildPlayerOnlyResponse(uint64 PlayerId)
{
    TResponse Response;
    Response.PlayerId = PlayerId;
    return Response;
}

TVector<FObjectPersistenceRecord> ToProtocolPersistenceRecords(const TVector<SPersistenceRecord>& Records);

// AWAIT macro — MHeaderTool parses this and generates .Then() chains.
// The actual continuation implementation is generated to .mgenerated.cpp.
#define AWAIT(expr) (void)(expr)

// co_return macro — MHeaderTool parses this for error handling.
// In the generated code, this is replaced with Promise.SetValue() calls.
#define co_return return
