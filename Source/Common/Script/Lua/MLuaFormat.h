/**
 * @file MLuaFormat.h
 * @brief MLuaFormat —— Lua 侧字符串格式化桥接器声明。
 *
 * @detail Install 在 Mession 下建 Format 子表,暴露 fmt / concat / split /
 *   trim / tostring 五个入口;实现见 MLuaFormat.cpp。
 */
#pragma once

#include "Common/Runtime/MLib.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    /**
     * @brief MLuaFormat - 把 Lua 的 Mession.Format.* 调用转发到 fmt / MStringUtil。
     *
     * @detail fmt 使用 fmt-style 的 {} 占位符,底层是 fmt::vformat(见 MLuaFormat.cpp)。
     */
    class MLuaFormat {
        public:
        /**
         * @brief Install - 在当前 VM 上注册 Mession.Format.{fmt, concat, split, trim, tostring}。
         * @param L 目标 Lua 状态机
         */
        static void Install(lua_State* L);
    };

} // namespace mession::script::lua