#include "Common/Script/Lua/LuaHotReload.h"
#include "Common/Script/Lua/LuaEngine.h"

namespace mession::script::lua {

TResult<EReloadResult> MLuaHotReload::ReloadDualVM(MLuaEngine& /*Engine*/, const MString& /*NewBytes*/)
{
    // 占位:实现走
    // 1. 创建 NewVM
    // 2. 旧 VM 协程列表(FPendingCall.IsPending)
    // 3. 等所有协程完成,带 30s 超时
    // 4. lua_close 旧 VM,NewVM 替换
    return TResult<EReloadResult>::Ok(EReloadResult::Success);
}

TResult<EReloadResult> MLuaHotReload::ReloadAtomicSwap(MLuaEngine& /*Engine*/, const MString& /*NewBytes*/)
{
    return TResult<EReloadResult>::Ok(EReloadResult::Success);
}

} // namespace mession::script::lua