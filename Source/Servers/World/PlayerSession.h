#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

class MPlayerAvatar;

MCLASS()
class MPlayerSession : public MObject
{
public:
    MGENERATED_BODY(MPlayerSession, MObject, 0)

public:
    MPlayerSession() { SetClass(StaticClass()); }

    MPROPERTY(SaveGame)
    uint64 PlayerId = 0;

    MPROPERTY(SaveGame)
    MString Name;

    uint64 GatewayConnectionId = 0;
    uint32 SessionKey = 0;
    MPlayerAvatar* Avatar = nullptr;
    uint32 CurrentSceneId = 0;
    bool bOnline = false;
};
