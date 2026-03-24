#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Mgo/MgoPlayerStateMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

MCLASS(Type=Service)
class MMgoPlayerStateServiceEndpoint : public MObject
{
public:
    MGENERATED_BODY(MMgoPlayerStateServiceEndpoint, MObject, 0)
public:
    void Initialize(TMap<uint64, TVector<FMgoPersistenceRecord>>* InPlayerRecords);

    MFUNCTION(ServerCall)
    MFuture<TResult<FMgoLoadPlayerResponse, FAppError>> LoadPlayer(const FMgoLoadPlayerRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FMgoSavePlayerResponse, FAppError>> SavePlayer(const FMgoSavePlayerRequest& Request);

private:
    TMap<uint64, TVector<FMgoPersistenceRecord>>* PlayerRecords = nullptr;
};
