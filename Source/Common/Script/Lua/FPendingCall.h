#pragma once

#include "Common/Runtime/MLib.h"

extern "C" {
struct lua_State;
}

namespace mession::script::lua {

    class FLuaPendingCall {
        public:
        FLuaPendingCall()  = default;
        ~FLuaPendingCall() = default;

        FLuaPendingCall(const FLuaPendingCall&)            = delete;
        FLuaPendingCall& operator=(const FLuaPendingCall&) = delete;

        void InitYield(lua_State* InL, int32 InNArgs);
        void Resume(const MString* OutVals, size_t OutCount, MString* OutErrs, size_t ErrCount);

        bool IsPending() const {
            return bPending;
        }
        void Cancel() {
            bPending = false;
        }

        private:
        lua_State* L         = nullptr;
        int32      NArgs     = 0;
        int32      ThreadRef = -1;
        bool       bPending  = false;
    };

} // namespace mession::script::lua