#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

namespace mession::script {

    enum class EReloadResult : uint8 {
        Success = 0,
        Partial = 1,
        Failed  = 2,
    };

} // namespace mession::script

MENUM(EReloadResult)