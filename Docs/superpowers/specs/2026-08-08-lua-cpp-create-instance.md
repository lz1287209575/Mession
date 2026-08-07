# Lua 反向桥:CreateLuaInstance Spec

日期:2026-08-08
作者:反向桥接 spec
状态:DRAFT
范围:`IScriptEngine::CreateLuaInstance` 新 API —— C++ 根据 Lua class name 字符串创建 Lua class 实例,返回 userdata proxy(持 registry ref + weak ref 自动失效)。

---

## 1. 总览

| 维度 | 决策 |
|---|---|
| API 路径 | `IScriptEngine::CreateLuaInstance(className, args)` |
| 工厂约定 | `:new(args)` 优先,fallback `__call(args)`(若 `:new` 不存在) |
| 返回值 | Lua userdata(持 registry ref + metatable `__index/__newindex/__gc`) |
| 失败 | className 不存在 / 工厂抛错 → 返回 Err,无 userdata |
| 生命周期 | weak ref 在 Lua GC 释放原对象时自动清;C++ 端不需 Unpin |
| 业务侧心智 | 与 C++ 侧 `Player:new(args)` 等价;Lua 侧 `m.name` / `m:method()` 走 metatable |

业务侧心智:

```lua
-- Lua 侧 class 定义
local Monster = {}
Monster.__index = Monster
function Monster:new(name, hp)
    return setmetatable({name = name, hp = hp or 100}, Monster)
end
function Monster:take_damage(d)
    self.hp = self.hp - d
    return self.hp
end

-- 业务调用(在 Lua 侧)
Mession.CallC("MLuaEngine", "CreateLuaInstance", "Monster", "goblin", 50)
-- 栈顶是 userdata proxy,可访问字段 / 方法
```

C++ 侧使用流程:

```cpp
auto R = Engine.CreateLuaInstance("Monster", Args);
if (R.IsErr()) return error;
TLuaInstanceHandle H = R.GetValue();

// H 通过 C++ 持有;Lua GC 释放原对象 → H 失效(下次访问抛错)

// 释放时:
luaL_unref(L, LUA_REGISTRYINDEX, H.GetRef());
```

---

## 2. 接口扩展

### 2.1 `IScriptEngine` 新增方法

```cpp
class IScriptEngine {
public:
    // 已有(反射侧 C++ class)
    virtual TResult<MObject*> CreateInstance(MClass* Cls, const TScriptArgs& Args) = 0;

    // 新增(Lua class 侧 Lua 类)
    virtual TResult<TLuaInstanceHandle> CreateLuaInstance(
        const MString& LuaClassName,
        const TScriptArgs& Args) = 0;
};
```

### 2.2 `TLuaInstanceHandle`

`Source/Common/Script/Abstract/TLuaInstanceHandle.h`:

```cpp
#pragma once

#include "Common/Runtime/MLib.h"

extern "C" {
struct lua_State;
}

namespace mession::script {

// TLuaInstanceHandle — C++ 持有的 Lua 实例 handle
// 内部是 luaL_ref 在 LUA_REGISTRYINDEX 的 ref key
// 通过 MLuaInstanceProxy metatable 转发访问 Lua 原对象
class TLuaInstanceHandle
{
public:
    TLuaInstanceHandle() = default;
    explicit TLuaInstanceHandle(int RegistryRef) : Ref(RegistryRef) {}

    bool IsValid() const { return Ref != LUA_NOREF; }
    int  GetRef() const { return Ref; }

    // 用于跨进程 RPC 序列化,后续 plan 扩展
    void SetRef(int InRef) { Ref = InRef; }

private:
    int Ref = -1;  // LUA_NOREF
};

} // namespace mession::script
```

---

## 3. userdata + metatable 设计

### 3.1 userdata 结构

```cpp
// C++ 端(放 LuaInstanceProxy.cpp)
struct FLuaInstance
{
    int Ref;  // LUA_REGISTRYINDEX 的 ref key
};
```

### 3.2 metatable `MLuaInstanceProxy`

```lua
-- 在 Resources/instance_init.lua 注册
MInstanceProxy = {
    __index = function(t, k)
        local ref = rawget(t, "__ref__")
        if ref == nil then return nil end
        local obj = rawget(LUA_REGISTRY, ref)
        if obj == nil then return nil end
        return obj[k]
    end,
    __newindex = function(t, k, v)
        local ref = rawget(t, "__ref__")
        local obj = rawget(LUA_REGISTRY, ref)
        if obj == nil then return end
        rawset(obj, k, v)
    end,
    __gc = function(t)
        local ref = rawget(t, "__ref__")
        if ref then
            luaL_unref(LUA_REGISTRY, ref)
            rawset(t, "__ref__", nil)
        end
    end,
    __tostring = function(t)
        local ref = rawget(t, "__ref__")
        if ref then
            local obj = rawget(LUA_REGISTRY, ref)
            if obj then return tostring(obj) end
        end
        return "<LuaInstance:GC'd>"
    end,
}
```

### 3.3 C++ 创建 Lua 实例 + wrap userdata

```cpp
TResult<TLuaInstanceHandle> MLuaEngine::CreateLuaInstance(
    const MString& LuaClassName, const TScriptArgs& Args)
{
    lua_State* L = State->GetLuaState();
    int OldTop = lua_gettop(L);

    // 1. 拿 Lua class
    lua_getglobal(L, LuaClassName.c_str());
    if (!lua_istable(L, -1))
    {
        lua_settop(L, OldTop);
        return TResult<TLuaInstanceHandle>::Err(MString("class_not_found: ") + LuaClassName);
    }

    // 2. 选工厂:优先 :new,fallback __call
    bool bUseNew = false;
    lua_getfield(L, -1, "new");
    bUseNew = lua_isfunction(L, -1);
    lua_pop(L, 1);  // pop new

    if (bUseNew)
    {
        // class.new(args):class 作 self
        lua_getfield(L, -1, "new");              // stack: [class, new]
        lua_insert(L, -2);                       // stack: [new, class]
        for (size_t i = 0; i < Args.Count; ++i)
        {
            PushVariant(L, Args.Values[i]);
        }
        if (lua_pcall(L, (int)Args.Count + 1, 1, 0) != LUA_OK)
        {
            MString Err = lua_tostring(L, -1);
            lua_settop(L, OldTop);
            return TResult<TLuaInstanceHandle>::Err(Err);
        }
    }
    else
    {
        // class(args):触发 __call metatable
        // stack 已有 [class]。push args。
        for (size_t i = 0; i < Args.Count; ++i)
        {
            PushVariant(L, Args.Values[i]);
        }
        if (lua_pcall(L, (int)Args.Count, 1, 0) != LUA_OK)
        {
            MString Err = lua_tostring(L, -1);
            lua_settop(L, OldTop);
            return TResult<TLuaInstanceHandle>::Err(Err);
        }
    }

    // 3. 栈顶是 Lua instance。wrap 成 userdata。
    FLuaInstance* I = (FLuaInstance*)lua_newuserdata(L, sizeof(FLuaInstance));
    luaL_setmetatable(L, InstanceMetaRef);  // 设 metatable

    // 4. 把 Lua 实例塞进 registry,userdata.__ref__ 指向 registry ref。
    lua_insert(L, -2);  // stack: [lua_instance, userdata]
    I->Ref = luaL_ref(L, LUA_REGISTRYINDEX);  // pop lua_instance,store ref

    // 5. 设 userdata.__ref__ = I->Ref(在 metatable __index 转 ref 即可,无需单独 set)

    // 6. 弹出 userdata 给 C++
    FLuaInstance* Out = (FLuaInstance*)lua_touserdata(L, -1);
    int Ref = Out->Ref;
    lua_settop(L, OldTop);

    return TResult<TLuaInstanceHandle>::Ok(TLuaInstanceHandle(Ref));
}
```

---

## 4. 释放路径

C++ 端释放:

```cpp
void MLuaEngine::ReleaseLuaInstance(TLuaInstanceHandle Handle)
{
    if (!Handle.IsValid()) return;
    if (!State || !State->IsValid()) return;
    luaL_unref(State->GetLuaState(), LUA_REGISTRYINDEX, Handle.GetRef());
}
```

简化版本(本 spec):C++ 不强制调 `ReleaseLuaInstance`,因为 Lua GC 会调 `__gc` 元方法。但**显式释放更可控** —— `TLuaInstanceHandle` 应该由 `TUniquePtr` / RAII 包装,在析构时调。

**推荐 RAII 包装**:

```cpp
class TLuaInstanceGuard
{
    TSharedPtr<IScriptEngine> Engine;
    TLuaInstanceHandle        Handle;
public:
    ~TLuaInstanceGuard()
    {
        if (Engine) Engine->ReleaseLuaInstance(Handle);
    }
};
```

---

## 5. 失败处理

| 失败 | C++ 看到 |
|---|---|
| className 不存在 | `Err("class_not_found: Monster")` |
| `:new` 抛错 | `Err(<Lua 错误消息>)` |
| `:new` 返回 nil | `Err("factory_returned_nil")` |
| Lua GC 已释放原对象 | 后续访问 metatable `__index` 返回 nil,或 C++ 端访问抛 "instance was GC'd" |

---

## 6. 测试要求

`TestLuaCreateInstance.cpp` 覆盖:

1. `TestCreateByNew`:Lua class 含 `:new`,C++ 调 CreateLuaInstance 返回 handle,handle 可访问字段
2. `TestCreateByCall`:Lua class 只有 `__call`,无 `:new`,fallback 成功
3. `TestCreateNotFound`:className 不存在 → Err
4. `TestCreateNewThrows`:`:new` 抛错 → 错误消息透传
5. `TestCreateNilReturn`:`:new` 返回 nil → Err
6. `TestAccessAfterGC`:Lua 端 collectgarbage,userdata metatable `__index` 返回 nil
7. `TestRelease`:C++ 调 ReleaseLuaInstance,registry ref 释放
8. `TestMethodCall`:userdata 上 `:method()` 调原对象方法

---

## 7. 验收标准

- ✅ C++ 端用 `Mession.CreateLuaInstance("Monster", Args)` 创建 Lua 类实例,返回 handle
- ✅ Handle 可在 Lua 业务侧访问字段 / 调方法
- ✅ Lua GC 释放原对象后,handle 访问自动失效(不崩)
- ✅ 工厂 `:new` 优先,`__call` fallback,两种语义都能跑
- ✅ className 不存在 / 工厂抛错 → 走 Err 路径

---

## 8. 风险与降级

| 风险 | 降级 |
|---|---|
| Lua GC 释放原对象后,C++ 持失效 ref | metatable `__index` 检测 nil,显式报错 |
| C++ 不释放 userdata → 内存泄漏 | 提供 `TLuaInstanceGuard` RAII 包装 |
| `:new` 内部 yield(异步) | 本 spec **不支持**,同步路径;后续 spec 加 lua_yield 桥 |
| 多线程同时访问同一个 handle | Lua 本身非线程安全;用户需保证同一 L 单线程 |

---

## 9. 工作量估算

| 子任务 | 工作量 |
|---|---|
| `TLuaInstanceHandle.h` + IScriptEngine 接口扩展 | 0.25 人日 |
| `MLuaEngine::CreateLuaInstance` + metatable 实现 | 1 人日 |
| `Resources/instance_init.lua`(MLuaInstanceProxy metatable) | 0.5 人日 |
| `TestLuaCreateInstance.cpp` 8 个测试 | 0.5 人日 |
| `ReleaseLuaInstance` + RAII Guard | 0.25 人日 |
| **合计** | **2.5 人日** |

---

## 10. 关联文档

- 抽象层:`2026-08-04-script-engine-abstract.md`(IScriptEngine 接口)
- 反射桥:`2026-08-07-lua-reflect-bridge.md`(兄弟 spec,双向桥接)
- Lua 嵌入:`2026-08-05-lua-impl.md`(Lua 5.4 + Teal 总 plan)
- 后续 plan:本 spec 落地的 plan