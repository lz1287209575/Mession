#pragma once

#include "Common/Runtime/MLib.h"

namespace mession::script {

enum class EReloadMode : uint8
{
    AtomicSwap = 0,
    DualVM     = 1,
    Discard    = 2,
};

} // namespace mession::script