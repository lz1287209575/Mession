#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Mgo/MgoPlayerStateMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

MCLASS(Type=Service)
class MMgoPlayerState : public MObject
{
public:
    MGENERATED_BODY(MMgoPlayerState, MObject, 0)
public:
    void Initialize(TMap<uint64, TVector<FObjectPersistenceRecord>>* InPlayerRecords);

    MFUNCTION(ServerCall)
    MFuture<TResult<FMgoLoadPlayerResponse, FAppError>> LoadPlayer(const FMgoLoadPlayerRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FMgoSavePlayerResponse, FAppError>> SavePlayer(const FMgoSavePlayerRequest& Request);

private:
    TMap<uint64, TVector<FObjectPersistenceRecord>>* PlayerRecords = nullptr;
};

