#pragma once

#include "Common/Runtime/MLib.h"

namespace mession::script::lua {

    class MLuaEngine;

    class MLuaMobDebugServer {
        public:
        static void Enable(MLuaEngine& Engine, uint16 Port = 9339);
        static void Disable();
    };

} // namespace mession::script::lua