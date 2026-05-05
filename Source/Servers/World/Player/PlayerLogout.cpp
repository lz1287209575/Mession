#include "Servers/World/Player/PlayerService.h"

#include "Common/Runtime/Concurrency/FiberAwait.h"
#include "Common/Runtime/Persistence/PersistenceSubsystem.h"
#include "Protocol/Messages/Mgo/MgoPlayerStateMessages.h"
#include "Servers/World/Player/Player.h"

namespace
{
template<typename TResponse>
TResponse BuildPlayerOnlyResponse(uint64 PlayerId)
{
    TResponse Response;
    Response.PlayerId = PlayerId;
    return Response;
}

TVector<FObjectPersistenceRecord> ToProtocolPersistenceRecords(const TVector<SPersistenceRecord>& Records)
{
    TVector<FObjectPersistenceRecord> Result;
    Result.reserve(Records.size());
    for (const SPersistenceRecord& Record : Records)
    {
        Result.push_back(FObjectPersistenceRecord{
            Record.ObjectPath,
            Record.ClassName,
            Record.SnapshotData,
        });
    }
    return Result;
}
}  // namespace
