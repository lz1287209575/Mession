#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Lua/FPendingCall.h"

extern "C" {
#include <lua.h>
}

namespace mession::script::lua {

    class MLuaCoroutineBridge {
        public:
        static bool IsCoroutineYielded(lua_State* L);
        static void ResumeFromStack(lua_State* L, int32 NResults);
        static void ThrowError(lua_State* L, const MString& Msg);
    };

} // namespace mession::script::lua