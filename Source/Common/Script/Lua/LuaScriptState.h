#pragma once

#include "Common/Runtime/MLib.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace mession::script::lua {

    class MLuaScriptState {
        public:
        MLuaScriptState();
        ~MLuaScriptState();

        MLuaScriptState(const MLuaScriptState&)            = delete;
        MLuaScriptState& operator=(const MLuaScriptState&) = delete;

        lua_State* GetLuaState() const {
            return L;
        }
        bool IsValid() const {
            return L != nullptr;
        }

        // 加载并执行字节流;返回 Err("") 表示成功
        MString LoadBuffer(const MString& Name, const char* Bytes, size_t Size);

        private:
        lua_State* L = nullptr;
    };

} // namespace mession::script::lua