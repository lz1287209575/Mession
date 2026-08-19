#include "Common/Script/Lua/LuaScriptState.h"
#include "Common/Runtime/Log/Log.h"

namespace mession::script::lua {

    MLuaScriptState::MLuaScriptState() {
        L = luaL_newstate();
        if (!L) {
            LOG_ERROR("LuaScriptState: luaL_newstate returned nullptr");
            return;
        }
        luaL_openlibs(L);
    }

    MLuaScriptState::~MLuaScriptState() {
        if (L) {
            lua_close(L);
            L = nullptr;
        }
    }

    void MLuaScriptState::SetOpaque(void* P) {
        if (!L) return;
        // Lua 5.4 extraspace:前 sizeof(void*) 字节存 opaque pointer
        // (LUA_EXTRASPACE 默认 20 字节,够用)
        void** Extraspace = static_cast<void**>(lua_getextraspace(L));
        Extraspace[0] = P;
    }

    void* MLuaScriptState::GetOpaque() const {
        if (!L) return nullptr;
        void** Extraspace = static_cast<void**>(lua_getextraspace(L));
        return Extraspace[0];
    }

    MString MLuaScriptState::LoadBuffer(const MString& Name, const char* Bytes, size_t Size) {
        int RC = luaL_loadbufferx(L, Bytes, Size, Name.c_str(), nullptr);
        if (RC != LUA_OK) {
            MString Err = lua_tostring(L, -1);
            lua_pop(L, 1);
            return Err.empty() ? MString("lua_load_failed") : Err;
        }

        RC = lua_pcall(L, 0, LUA_MULTRET, 0);
        if (RC != LUA_OK) {
            MString Err = lua_tostring(L, -1);
            lua_pop(L, 1);
            return Err.empty() ? MString("lua_pcall_failed") : Err;
        }

        return MString();
    }

} // namespace mession::script::lua