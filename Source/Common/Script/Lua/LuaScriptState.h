/**
 * @file LuaScriptState.h
 * @brief MLuaScriptState —— 一个 lua_State 的 RAII 所有者。
 *
 * @detail 构造即 luaL_newstate + luaL_openlibs,析构 lua_close;不可拷贝。
 *   另外负责 extraspace(VM 级 raw memory)的读写,供 DebugHook 反查宿主对象。
 */
#pragma once

#include "Common/Runtime/MLib.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace mession::script::lua {

    /**
     * @brief MLuaScriptState - 单个 Lua VM 的所有权封装。
     *
     * @detail 非线程安全:同一个 MLuaScriptState 只能由一个线程访问。
     */
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

        /**
         * @brief LoadBuffer - 加载并执行一段 chunk。
         * @param Name  chunk 名(报错定位用)
         * @param Bytes 源码字节(不必 NUL 结尾)
         * @param Size  字节数
         * @return 空串表示成功;否则是 VM 的加载/运行错误文本
         *
         * @detail 分两步:luaL_loadbufferx 编译 + lua_pcall 执行。只做最外层调用,
         *   不装 traceback,也不清理 pcall 的返回值(LUA_MULTRET 会留在栈上)。
         */
        MString LoadBuffer(const MString& Name, const char* Bytes, size_t Size);

        /**
         * @brief SetOpaque - 把宿主指针写进 VM 的 extraspace。
         *
         * @detail LUA_EXTRASPACE 在本仓库的 vendored Lua 里等于 sizeof(void*);写入的是
         *   extraspace 首槽,供 DebugHook 反查宿主(MLuaEngine)。L 为空时静默返回。
         */
        void SetOpaque(void* P);
        /** @brief GetOpaque - 读回 SetOpaque 写入的指针;未设置或 L 为空时返回 nullptr */
        void* GetOpaque() const;

        private:
        lua_State* L = nullptr; // nullptr 表示 luaL_newstate 失败,IsValid() 为 false
    };

} // namespace mession::script::lua