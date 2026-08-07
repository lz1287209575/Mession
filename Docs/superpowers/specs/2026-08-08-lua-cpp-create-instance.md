# Script 实例句柄 Spec(抽象层通用)

日期:2026-08-08
作者:抽象层反向桥接 spec
状态:已实现(Lua 5.4 落地)
范围:`IScriptEngine::CreateInstanceByClassName` / `InvokeInstanceMethod` / `ReleaseInstance` 三个通用 API,让 C++ 框架层按字符串类名创建脚本侧 class 实例并长期持有。

---

## 1. 总览

| 维度 | 决策 |
|---|---|
| API 位置 | `IScriptEngine` 抽象层(`mession::script` namespace),跨 4 套 VM 通用 |
| Handle | `TScriptInstanceHandle`(opaque `int Id`,VM 内部映射) |
| 工厂约定 | `:new(args)` 优先,fallback `__call(args)` |
| 返回值 | `TResult<TScriptInstanceHandle>` — 失败时返回 Err,无 handle |
| 实现现状 | Lua 5.4(`mession_lua`);Python / TS / C# 留 TODO |

业务侧心智:

```lua
-- Lua 侧
local Monster = {}
Monster.__index = Monster
function Monster:new(name, hp)
    return setmetatable({name=name, hp=hp or 100}, Monster)
end

-- C++ 侧
auto H = Engine.CreateInstanceByClassName("Monster", Args);
Engine.InvokeInstanceMethod(H, "take_damage", DamageArgs);  -- 返回 TResult<TVariant>
Engine.ReleaseInstance(H);  -- 显式释放
```

---

## 2. 抽象层接口扩展

### 2.1 `TScriptInstanceHandle`(已落地)

`Source/Common/Script/Abstract/TScriptInstanceHandle.h`:

```cpp
namespace mession::script {
class TScriptInstanceHandle
{
public:
    static constexpr int InvalidId = -1;
    TScriptInstanceHandle() = default;
    explicit TScriptInstanceHandle(int InId) : Id(InId) {}
    bool IsValid() const { return Id != InvalidId; }
    int  GetId() const { return Id; }
    void SetId(int InId) { Id = InId; }  // 跨进程扩展点
private:
    int Id = InvalidId;
};
}
```

### 2.2 `IScriptEngine` 新增 3 个方法

```cpp
// ====== C++ 持有脚本侧 class 实例(跨 VM 通用) ======
virtual TResult<TScriptInstanceHandle> CreateInstanceByClassName(
    const MString& ClassName, const TScriptArgs& Args) = 0;

virtual TResult<TVariant> InvokeInstanceMethod(
    TScriptInstanceHandle Handle, const MString& MethodName,
    const TScriptArgs& Args) = 0;

virtual void ReleaseInstance(TScriptInstanceHandle Handle) = 0;
```

### 2.3 `ScriptErrorCodes` 新增

```cpp
inline constexpr const char* kClassNotFound    = "class_not_found";
inline constexpr const char* kFactoryReturnNil = "factory_returned_nil";
inline constexpr const char* kInstanceReleased = "instance_released";
inline constexpr const char* kMethodNotFound   = "method_not_found";
```

---

## 3. VM 各自的实现策略

| VM | 内部 ID → 对象映射 | 实现 |
|---|---|---|
| **Lua 5.4**(`mession_lua`) | `int` = `luaL_ref(LUA_REGISTRYINDEX)` | 已实现,见 `MLuaEngine::CreateInstanceByClassName` |
| **Python 3.12** | `int` → `PyObject*` 存 `TMap<int, TSharedPtr<PyObject>>` | TODO |
| **TypeScript via QuickJS** | `int` → `JSValue` 存 `TMap<int, JSValue>`,`JS_DupValue` / `JS_FreeValue` | TODO |
| **C# via CoreCLR** | `int` → `GCHandle`(strong) | TODO |

---

## 4. Lua 5.4 实现细节

### 4.1 `MLuaEngine::CreateInstanceByClassName`

```cpp
1. lua_getglobal(L, ClassName)                          -- 拿 Lua class
2. 若不是 table → Err("class_not_found: X")
3. lua_getfield(L, -1, "new");bUseNew = isfunction
4. bUseNew 路径:
   a. lua_getfield(L, -1, "new")                  -- push new
   b. lua_insert(L, -2)                            -- stack: [new, class]
   c. push args                                    -- stack: [new, class, arg1, ...]
   d. lua_pcall(L, N+1, 1, 0)                     -- class 作 self
5. fallback 路径:
   a. push args                                    -- stack: [class, arg1, ...]
   b. lua_pcall(L, N, 1, 0)                       -- 触发 __call(class, ...)
6. 栈顶 nil → Err("factory_returned_nil")
7. luaL_ref(L, LUA_REGISTRYINDEX) → Ref
8. 返回 Ok(TScriptInstanceHandle(Ref))
```

### 4.2 `MLuaEngine::InvokeInstanceMethod`

```cpp
1. Handle 无效 → Err("invalid_arg")
2. lua_rawgeti(L, LUA_REGISTRYINDEX, Handle.Id)
3. nil → Err("instance_released")
4. 类型不是 table/userdata → Err("invalid_arg: instance is not a table/userdata")
5. lua_getfield(L, -1, MethodName) → 必须 function
6. 不是 function → Err("method_not_found: X")
7. lua_insert(L, -2)  -- stack: [method, self]
8. push args  -- stack: [method, self, arg1, ...]
9. lua_pcall(L, N+1, 1, 0)
10. 失败 → Err(<Lua 错误>)
11. 栈顶结果 → TVariant(5 类:Null/Bool/Int/Double/String)
```

### 4.3 `MLuaEngine::ReleaseInstance`

```cpp
luaL_unref(L, LUA_REGISTRYINDEX, Handle.Id);
```

### 4.4 错误传递

`ScriptErrorCodes` 常量 + 业务可见的 `MString` 错误消息,不暴露 Lua 内部细节。

---

## 5. 测试(`TestLuaInstanceHandle.cpp`)

8 个测试,全部通过:

1. `TestBasicCreate` — `:new` 路径,class Monster(create)
2. `TestFallbackCall` — `__call` fallback,class Adder(无 `:new`)
3. `TestClassNotFound` — 不存在的 class(应返回 Err)
4. `TestNewThrows` — `:new` 抛 ""boom""(错误消息透传)
5. `TestMethodCall` — InvokeInstanceMethod 返回值正确转换
6. `TestMethodNotFound` — 不存在方法(Err)
7. `TestReleaseAfterGC` — Lua GC 触发后 Invoke(不 panic)
8. `TestReleaseExplicit` — ReleaseInstance 后 Invoke(Err)

---

## 6. 验收标准

✅ Lua class 含 `:new` → CreateInstanceByClassName 调用 `:new`,返回 handle
✅ Lua class 含 `__call` 但没 `:new` → fallback 成功
✅ className 不存在 → Err("class_not_found: X")
✅ `:new` 抛错 → 错误消息透传
✅ userdata 可被 Lua 业务侧访问字段 / 调方法(invoke 路径)
✅ Lua GC 释放原对象后,handle 访问不 panic
✅ 抽象层 `IScriptEngine` 12 → 15 方法,既有接口零变动

---

## 7. 后续 plan(本 spec 不做)

- Python 3.12 `PyObject*` 实现 — 独立 plan,~1.5 人日
- TypeScript via QuickJS `JSValue` 实现 — 独立 plan,~1.5 人日
- C# via CoreCLR `GCHandle` 实现 — 独立 plan,~1.5 人日
- 跨进程序列化(`SetId` 扩展) — 独立 spec,远超本 plan 范围
- `TScriptInstanceGuard` RAII 包装(可选,~0.25 人日)

---

## 8. 关联文档

- 抽象层:`2026-08-04-script-engine-abstract.md`(IScriptEngine 接口)
- 反射桥:`2026-08-07-lua-reflect-bridge.md`(兄弟 spec,双向桥接)
- Lua 嵌入:`2026-08-05-lua-impl.md`(Lua 5.4 + Teal 总 plan)
- Plan:`/root/.claude/plans/humming-chasing-sonnet.md`(本 spec 落地)
- Ledger:TODO(在 task 完成后填)