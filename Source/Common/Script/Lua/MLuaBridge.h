#pragma once

#include "Common/Runtime/MLib.h"

extern "C" {
struct lua_State;
}

namespace mession::script::lua {

    class MLuaEngine;

    // MLuaBridge — 暴露 M.InvokeStatic / M.GetObject 等 Lua 全局 cfunction
    // 给 LuaBindEmitter emit 出来的 .lua 文件调用,代替之前 phantom 的
    // M.InvokeClass / M.RegisterClass。
    //
    // 使用方式:
    //   MLuaBridge::Install(L, &engine);
    //   之后 Lua 内:
    //     M.InvokeStatic("<ClassName>", "<MethodName>", {...})
    //     M.GetObject(<id>)            -- Phase 2 才用
    //
    // Phase 1 范围:static dispatch(self=nullptr)+ 4 个 primitive args/return
    // (int32/int64/double/bool/MString)。复杂类型 args/return 返回 Err
    // 字符串。
    class MLuaBridge {
        public:
        static void Install(lua_State* L, MLuaEngine* Engine);
    };

} // namespace mession::script::lua