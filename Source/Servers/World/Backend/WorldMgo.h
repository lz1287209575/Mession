#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Mgo/MgoPlayerStateMessages.h"
#include "Servers/App/ServerCallProxy.h"

MCLASS(Type=Rpc)
class MWorldMgo : public MServerCallProxyBase
{
public:
    MGENERATED_BODY(MWorldMgo, MServerCallProxyBase, 0)
public:
    MFUNCTION(ServerCall, Target=Mgo)
    MFuture<TResult<FMgoLoadPlayerResponse, FAppError>> LoadPlayer(const FMgoLoadPlayerRequest& Request);

    MFUNCTION(ServerCall, Target=Mgo)
    MFuture<TResult<FMgoSavePlayerResponse, FAppError>> SavePlayer(const FMgoSavePlayerRequest& Request);

private:
    EServerType GetTargetServerType() const override;
};

