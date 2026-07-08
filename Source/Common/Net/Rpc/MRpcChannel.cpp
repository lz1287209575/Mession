#include "Common/Net/Rpc/MRpcChannel.h"

MRpcChannel& MRpcChannel::Get()
{
    static MRpcChannel Instance;
    return Instance;
}
