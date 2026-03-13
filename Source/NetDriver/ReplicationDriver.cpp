#include "ReplicationDriver.h"

MReplicationDriver::~MReplicationDriver()
{
    for (auto& [Id, Channel] : Channels)
    {
        delete Channel;
    }
    Channels.clear();
}
