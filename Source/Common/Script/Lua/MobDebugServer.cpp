#include "Common/Script/Lua/MobDebugServer.h"
#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Runtime/Log/Log.h"

namespace mession::script::lua {

void MLuaMobDebugServer::Enable(MLuaEngine& /*Engine*/, uint16 /*Port*/)
{
    LOG_WARN("MobDebugServer: stub (Task 11 placeholder)");
}

void MLuaMobDebugServer::Disable() {}

} // namespace mession::script::lua