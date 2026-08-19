#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Lua/FPendingCall.h"

extern "C" {
#include <lua.h>
}

namespace mession::script::lua {

    // ECoroutineResumeStatus — ResumeFromStack 返回值
    // Ok      = coroutine returned normally (finished)
    // Yield   = coroutine yielded, still pending
    // Error   = coroutine raised an error
    enum class ECoroutineResumeStatus : uint8 {
        Ok      = 0,
        Yield   = 1,
        Error   = 2,
        Invalid = 3, // lua_State null or invalid
    };

    class MLuaCoroutineBridge {
        public:
        static bool IsCoroutineYielded(lua_State* L);
        // Resume: 用栈上 [1..NArgs] 当 resume args,返回状态
        static ECoroutineResumeStatus ResumeFromStack(lua_State* L, int32 NArgs, int32 FromStackIdx = 1);
        static void                   ThrowError(lua_State* L, const MString& Msg);
    };

} // namespace mession::script::lua