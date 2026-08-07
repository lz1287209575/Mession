#pragma once

#include "Common/Runtime/MLib.h"

namespace mession::script {

// TScriptInstanceHandle — C++ 持有脚本侧 class 实例的通用 handle
// 跨 VM 通用:VM 内部将 int Id 映射到自己的对象引用
//   Lua:    luaL_ref(LUA_REGISTRYINDEX)
//   Python: PyObject* 存到 VM 内部 map
//   TS:     JSValue + JS_DupValue
//   C#:     GCHandle.Alloc(obj, Strong)
//
// 跨进程语义:仅同进程;SetId 留扩展点,跨进程需要 class hash + 引用关系
//   是独立 spec 的范围,不在本 plan
class TScriptInstanceHandle
{
public:
    static constexpr int InvalidId = -1;

    TScriptInstanceHandle() = default;
    explicit TScriptInstanceHandle(int InId) : Id(InId) {}

    bool IsValid() const { return Id != InvalidId; }
    int  GetId() const { return Id; }
    void SetId(int InId) { Id = InId; }

private:
    int Id = InvalidId;
};

} // namespace mession::script