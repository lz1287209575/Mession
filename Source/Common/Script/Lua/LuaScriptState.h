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

        // Extraspace 桥接(DualVM + MobDebug 用):把 opaque 指针存在 lua_State 的 extraspace
        // Lua 5.4 的 lua_getextraspace 返回 LUA_EXTRASPACE 字节的 raw memory
        // 我们用前 sizeof(void*) 字节存 engine pointer,DebugHook 可以拿到
        void SetOpaque(void* P);
        void* GetOpaque() const;

        private:
        lua_State* L = nullptr;
    };

} // namespace mession::script::lua