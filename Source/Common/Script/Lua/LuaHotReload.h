#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Abstract/EReloadResult.h"

namespace mession::script::lua {

    class MLuaEngine;

    class MLuaHotReload {
        public:
        static TResult<EReloadResult> ReloadDualVM(MLuaEngine& Engine, const MString& NewBytes);
        static TResult<EReloadResult> ReloadAtomicSwap(MLuaEngine& Engine, const MString& NewBytes);
    };

} // namespace mession::script::lua