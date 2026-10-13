/**
 * @file LuaScriptState.cpp
 * @brief 实现 MLuaScriptState:VM 创建/销毁、extraspace 读写、chunk 加载执行。
 */
#include "Common/Script/Lua/LuaScriptState.h"
#include "Common/Runtime/Log/Log.h"

namespace mession::script::lua {

    /**
     * @brief 构造 - 创建 VM 并打开标准库。
     *
     * @detail luaL_newstate 失败时留 L=nullptr 并记一条 error 日志(不抛异常);
     *   调用方须用 IsValid() 判定。
     */
    MLuaScriptState::MLuaScriptState() {
        L = luaL_newstate();
        if (!L) {
            LOG_ERROR("LuaScriptState: luaL_newstate returned nullptr");
            return;
        }
        luaL_openlibs(L);
    }

    /**
     * @brief 析构 - 关闭 VM。
     *
     * @detail 触发所有 Lua 侧 __gc;此后任何指向该 VM 的 lua_State* 都失效。
     */
    MLuaScriptState::~MLuaScriptState() {
        if (L) {
            lua_close(L);
            L = nullptr;
        }
    }

    /**
     * @brief SetOpaque - 把宿主指针写入 VM extraspace 首槽。
     * @param P 任意宿主指针(通常是 MLuaEngine*);可为 nullptr
     *
     * @detail LUA_EXTRASPACE 在本仓库的 vendored Lua 里等于 sizeof(void*),
     *   正好容纳一个宿主指针;写入的是 extraspace 的首槽。
     */
    void MLuaScriptState::SetOpaque(void* P) {
        if (!L)
            return;
        void** Extraspace = static_cast<void**>(lua_getextraspace(L));
        Extraspace[0]     = P;
    }

    void* MLuaScriptState::GetOpaque() const {
        if (!L)
            return nullptr;
        void** Extraspace = static_cast<void**>(lua_getextraspace(L));
        return Extraspace[0];
    }

    /**
     * @brief LoadBuffer - 编译并执行 chunk。
     * @param Name  chunk 名
     * @param Bytes 源码;Size 为字节数
     * @return 空串成功;否则为 luaL_loadbufferx / lua_pcall 的错误信息
     *
     * @detail pcall 用 LUA_MULTRET,chunk 的返回值会留在栈上不清理;
     *   错误路径会把错误字符串 pop 掉。
     */
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