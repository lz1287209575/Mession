#pragma once

#include "Common/Runtime/MLib.h"

struct SWorldConfig
{
    uint16 ListenPort = 8003;
    uint16 SceneServerPort = 8004;
    MString RouterServerAddr = "127.0.0.1";
    uint16 RouterServerPort = 8005;
    MString LoginServerAddr = "127.0.0.1";
    uint16 LoginServerPort = 8002;
    MString ServerName = "World01";
    uint32 MaxPlayers = 10000;
    uint16 ZoneId = 0;
    uint16 DebugHttpPort = 0;
    bool EnableMgoPersistence = false;
    uint32 OwnerServerId = 3;
};
