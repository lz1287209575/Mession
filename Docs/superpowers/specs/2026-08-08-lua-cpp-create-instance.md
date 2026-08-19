# Script 实例句柄 Spec(抽象层通用)

日期:2026-08-08
作者:抽象层反向桥接 spec
状态:DRAFT(已落地)
范围:`IScriptEngine` 新 API —— C++ 框架层按字符串类名创建脚本侧 class 实例并长期持有。

---

## 1. 决策

| 维度 | 决策 |
|---|---|
| API 位置 | `IScriptEngine` 抽象层(`mession::script` namespace),跨 4 套 VM 通用 |
| Handle | `TScriptInstanceHandle`(opaque `int Id`,VM 内部映射到 registry ref / PyObject* / JSValue / GCHandle) |
| 工厂约定 | `:new(args)` 优先,fallback `__call(args)`(Lua 侧) |
| 返回值 | success → `TScriptInstanceHandle`;失败 → `TResult<MString>` Err |
| 实现现状 | Lua 5.4 已落地,`MLuaEngine::CreateInstanceByClassName` + `InvokeInstanceMethod` + `ReleaseInstance` |
| Python / TS / C# | 各自独立 plan,各 ~1.5 人日 |

业务侧心智:

```lua
-- Lua 侧 class(def 可能放在 lua_init.lua 或业务 .lua)
local Monster = {}
Monster.__index = Monster
function Monster:new(name, hp)
    return setmetatable({name=name, hp=hp}, self)
end

-- C++ 框架侧
auto R = Engine->CreateInstanceByClassName("Monster", Args);
if (R.IsErr()) return error;
TScriptInstanceHandle H = R.GetValue();

// 调方法
auto M = Engine->InvokeInstanceMethod(H, "take_damage", DamageArgs);
if (M.IsOk()) {
    int64 Damage = M.GetValue().AsInt().GetValue();
}

// CRUCIAL:显式释放
Engine->ReleaseInstance(H);
```

---

## 2. 接口签名

### 2.1 `CreateInstanceByClassName`

```cpp
TResult<TScriptInstanceHandle> CreateInstanceByClassName(
    const MString& ClassName,
    const TScriptArgs& Args);
```

| | |
|---|---|
| **ClassName** | Lua 全局类名 / Python `__name__` / TS 构造函数 |
| **Args** | 构造参数(std::vector<TVariant>) |
| **返回** | success → `TScriptInstanceHandle(Id)`,失败 → `MString` Err |
| **错误** | class 不存在 → `ScriptErrorCodes::kClassNotFound`;factory 抛 → pcall error msg;返回 nil → `kFactoryReturnNil` |

### 2.2 `InvokeInstanceMethod`

```cpp
TResult<TVariant> InvokeInstanceMethod(
    TScriptInstanceHandle Handle,
    const MString& MethodName,
    const TScriptArgs& Args);
```

| | |
|---|---|
| **Handle** | 来自 `CreateInstanceByClassName` |
| **MethodName** | 方法名 |
| **Args** | 调用参数 |
| **返回** | `TResult<TVariant>`(NULL 表示返回值 nil / void) |
| **错误** | handle 无效 → `kInstanceReleased`;方法不存在 → `kMethodNotFound`;调用失败 → pcall error msg |

### 2.3 `ReleaseInstance`

```cpp
void ReleaseInstance(TScriptInstanceHandle Handle);
```

| | |
|---|---|
| **Handle** | 已创建或 invoke 时的 handle |
| **效果** | VM 端 unref / DECREF / FreeValue;不抛异常(handle 无效时静默) |

---

## 3. `TScriptInstanceHandle`

```cpp
// Source/Common/Script/Abstract/TScriptInstanceHandle.h
namespace mession::script {

class TScriptInstanceHandle {
public:
    static constexpr int InvalidId = -1;

    TScriptInstanceHandle() = default;
    explicit TScriptInstanceHandle(int InId) : Id(InId) {}

    bool IsValid() const { return Id != InvalidId; }
    int  GetId()  const { return Id; }

private:
    int Id = InvalidId;
};

} // namespace mession::script
```

- **VM 内部**:每个 VM 维护 `int Id → VMObject*` map
- **Id 跨 VM 无意义**(同一进程不跨 VM)
- **Id 跨进程无意义**(纯 handle)

---

## 4. VM 各自的实现策略

| VM | 内部表示 | 释放 |
|---|---|---|
| **Lua 5.4** | `int → luaL_ref(LUA_REGISTRYINDEX)` | `luaL_unref(LUA_REGISTRYINDEX, Id)` |
| **Python 3.12** | `int → PyObject*` 存 `int → PyObject*` map | `Py_DECREF` |
| **TypeScript via QuickJS** | `int → JSValue` 存 weak ref table | `JS_FreeValue` |
| **C# via CoreCLR** | `int → GCHandle`(strong) | `GCHandle.Free()` |

### 4.1 Lua 5.4(已落地,`MLuaEngine.cpp`)

```cpp
// CreateInstanceByClassName 已完整实现:
//  1. lua_getglobal(L, ClassName) → 找 Lua 类
//  2. lua_getfield(L, -1, "new") → 探测 :new
//  3. push args + 1(class as self)
//  4. lua_pcall → 拿到 Lua 实例
//  5. luaL_ref 锁住防止 GC
//  6. 返回 handle
//
// 工厂为 :new 优先,无 :new 时尝试 __call(fallback)
// InvokeInstanceMethod 已完整实现:lua_rawgeti 拿实例,lua_getfield 拿方法,pcall
// 同步错误经 WrapException 包装成 kMethodNotFound 等
```

### 4.2 Python 3.12 (TODO)

```cpp
// 伪代码骨架
PyObject* PyClass = PyImport_ImportModule(...);  // 解析模块
PyObject* PyCallable = PyObject_GetAttrString(PyClass, "__init__");  // 或 __call__
PyObject* PyInstance = PyObject_Call(PyCallable, Args, NULL);
Py_DECREF(PyClass);
Py_DECREF(PyCallable);
int Ref = put_in_int_to_pyo_map(PyInstance);
return TScriptInstanceHandle(Ref);
```

### 4.3 TypeScript via QuickJS (TODO)

```cpp
// TS 走 __call__ 协议
JSValue Cls = JS_Eval(...);
JSValue Ctor = JS_GetPropertyStr(Cls, "constructor");
JSValue Ins = JS_Call(Cls, Ctor, ...);
JS_FreeValue(Ctor);
JS_FreeValue(Cls);
int Ref = ts_add_int_to_map(Ins);
```

### 4.4 C# via CoreCLR (TODO)

```cpp
// C# 走 Activator.CreateInstance
System::Type^ T = System::Type::GetType("MyApp." + ClassName);
System::Object^ Ins = System::Activator::CreateInstance(T);
int Ref = cs_put_in_int_to_obj_map(Ins);
```

---

## 5. 错误码补充

`ScriptErrorCodes.h` 已加:

```cpp
inline constexpr const char* kClassNotFound      = "class_not_found";
inline constexpr const char* kFactoryReturnNil   = "factory_returned_nil";
inline constexpr const char* kInstanceReleased   = "instance_released";
inline constexpr const char* kMethodNotFound     = "method_not_found";
```

---

## 6. 关联文档

- 抽象层:`2026-08-04-script-engine-abstract.md`
- 已有 `TScriptInstanceHandle` 已落地:`2026-08-08` 之前已 commit
- 反射桥 spec:`2026-08-07-lua-reflect-bridge.md`
- Vector 桥 spec:`2026-08-07-lua-vector-bridge.md`(userdata 持有 TVector)
- 反向桥接决策记录:对话历史 `2026-08-08` 之前已 commit
- 风险:`object-lifecycle.md` 的"业务侧不持长期对象" — 本 API 是**框架侧**持,反方向