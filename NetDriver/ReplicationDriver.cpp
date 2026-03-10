#include "ReplicationDriver.h"

UReplicationDriver::~UReplicationDriver()
{
    for (auto& [Id, Channel] : Channels)
    {
        delete Channel;
    }
    Channels.clear();
}
