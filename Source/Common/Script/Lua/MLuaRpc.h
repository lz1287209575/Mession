#pragma once

#include "Common/Runtime/MLib.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace mession::script::lua {

// MLuaRpc — Lua RPC + Time + Id 工具桥接器
// 用法:MLuaRpc::Install(L, &Engine) 注册
//   Mession.RPC.callById(FunctionId, ...)        → engine->CallFunctionById
//   Mession.Time.now()                           → 当前时间戳(秒,double)
//   Mession.Time.sleepMs(ms)                     → 阻塞 sleep
//   Mession.Id.new()                             → 唯一自增 ID
class IScriptRpcBridge; // forward decl — Engine 实现此接口
class MLuaRpc
{
public:
    // pEngine 必须存活到 lua_State 关闭
    static void Install(lua_State* L, IScriptRpcBridge* pEngine);
};

// 抽象接口 — MLuaEngine 实现此接口,让 MLuaRpc 调 CallFunction / CallFunctionById
// 不依赖 IScriptEngine.h,避免头文件耦合爆炸
class IScriptRpcBridge {
public:
    virtual ~IScriptRpcBridge() = default;
    // callByName:按 Lua 全局函数名直接调用(用于业务侧实现)
    virtual int CallGlobal(lua_State* L, const char* GlobalName, int ArgStart, int ArgEnd) = 0;
    // 返回 lua 调用结果(0 = 无错误,函数返回 N 个值到栈)
    // 错误时把错误消息 push 到栈顶
};

} // namespace mession::script::lua