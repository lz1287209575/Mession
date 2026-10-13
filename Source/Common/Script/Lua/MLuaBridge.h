/**
 * @file MLuaBridge.h
 * @brief MLuaBridge —— 在 Lua 侧暴露 M.InvokeStatic 的桥接器声明。
 *
 * @detail 供 LuaBindEmitter 生成的 .lua 调用,取代早期并不存在的
 *   M.InvokeClass / M.RegisterClass。
 */
#pragma once

#include "Common/Runtime/MLib.h"

extern "C" {
struct lua_State;
}

namespace mession::script::lua {

    class MLuaEngine;

    /**
     * @brief MLuaBridge - 在 Lua 侧暴露 M.InvokeStatic 的桥接器。
     *
     * @detail 用法:
     *     MLuaBridge::Install(L, &Engine);
     *     M.InvokeStatic("<ClassName>", "<MethodName>", {...})
     *   实参支持 5 种 primitive(int32/int64/double/bool/MString),并兼容两种传参
     *   形态(逐个展开的变参,或单个 args 表);其余类型报 arg_type_mismatch。
     *   NOTE: M.GetObject / M.LiveObjects 尚未实现 —— Install 只注册 InvokeStatic。
     */
    class MLuaBridge {
        public:
        /**
         * @brief Install - 注册反序列化表与 Lua 全局 M.InvokeStatic。
         * @param L      目标 Lua 状态机
         * @param Engine 所属引擎;NOTE 当前实现未使用该参数
         */
        static void Install(lua_State* L, MLuaEngine* Engine);
    };

} // namespace mession::script::lua