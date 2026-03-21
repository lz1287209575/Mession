#include "Servers/World/PlayerSession.h"
#include "Servers/World/Avatar/PlayerAvatar.h"
#include "MPlayerSession.mgenerated.h"

MPlayerAvatar* MPlayerSession::GetAvatar() const
{
    return dynamic_cast<MPlayerAvatar*>(GetOuter());
}
