# Lua-C++ 反射桥 Spec(分册 1/5)

日期:2026-08-07
作者:标准库桥接 spec 1/5
状态:DRAFT(基础分册)
范围:把 Mession C++ 反射系统(`MObject::FindClass` / `MClass::FindFunction` / `MFunction::Invoke`)桥接到 Lua 5.4,让业务侧能在脚本里用 `Mession.FindClass("MEchoService")` 查询类、`Mession.InvokeClass(class, method, args...)` 调用。

后续 4 份 spec(独立):
- 分册 2/5:Vector 桥
- 分册 3/5:Map 桥
- 分册 4/5:log / format 桥
- 分册 5/5:rpc / id / time 桥

---

## 1. 总览

| 维度 | 决策 |
|---|---|
| 桥接风格 | **Lua metatable** + `__index` 走反射查询 |
| 注册入口 | `Mession.*` global namespace |
| 错误传递 | 复用 `IScriptEngine::GetErrorString`;Lua 侧 `pcall` |
| 沙箱 | 默认**不沙箱**(个人项目 + 自己用) |
| 性能 | 热路径走 `MFunction::Invoke` 直接调,**不**走 Lua 解释 |

业务侧心智:

```lua
-- 查类
local MEchoService = Mession.FindClass("MEchoService")
print(MEchoService.Name)               -- 字段访问走 __index metatable → 反射 MClass->GetName()

-- 调函数(同步)
local resp = Mession.InvokeClass("MEchoService", "Echo", "Hello")
print(resp)                              -- 返回值通过 SFutureResult::IsOk/GetValue 桥接

-- 全局
Mession.SetGlobal("counter", 42)
local n = Mession.GetGlobal("counter")

-- 模块注册(框架侧调,业务侧不直接用)
Mession.RegisterModule("MEchoService", proxy_table)
```

---

## 2. 依赖与文件结构

新增:
```
Source/Common/Script/Lua/
├── MLuaReflectBridge.h                  # 反射桥头
├── MLuaReflectBridge.cpp                  # 实现
├── Resources/
│   └── reflect_init.lua                  # Mession.FindClass / InvokeClass 注入
└── Tests/
    └── TestLuaReflectBridge.cpp
```

修改:
```
Source/Common/Script/Lua/CMakeLists.txt   # 加 MLuaReflectBridge.cpp + Tests
Source/Common/Script/Lua/LuaEngine.cpp    # Initialize 调用 MLuaReflectBridge::Install
```

---

## 3. 反射桥 API 详细

### 3.1 `Mession.FindClass(name)`

| | |
|---|---|
| **签名** | `static MClass* FindClass(const MString& InName)` |
| **C++ 实现** | 调 `MObject::FindClass(InName.c_str())` |
| **Lua 调用** | `Mession.FindClass("MEchoService")` |
| **返回值** | 反射 proxy metatable(命名 `MClassProxy`,`__index` 走反射) |
| **nil 处理** | 类不存在 → 返回 `nil, kNotFound error` |

### 3.2 `Mession.InvokeClass(class, method, args...)`

| | |
|---|---|
| **签名** | `static SFutureResult<TResult<...>> InvokeClass(MClass* Cls, MName Method, TScriptArgs& Args)` |
| **Lua 调用** | `Mession.InvokeClass("MEchoService", "Echo", "Hello")` |
| **返回值** | `Lua` 侧:`nil` + error string(失败)/`(ok_value, nil)`(成功) |
| **实现** | 走 `MFunction::NativeInvoke` 或 `MFunction::ServerCallHandler` |
| **Lua 参数** | 标量直传;`MObject*` 通过 ActorId 间接(对象生命周期外讨论) |

### 3.3 `Mession.GetGlobal(key)` / `SetGlobal(key, value)`

| | |
|---|---|
| **GetGlobal 签名** | `static TResult<TVariant> GetGlobal(MStringView Key)` |
| **SetGlobal 签名** | `static void SetGlobal(MStringView Key, const TVariant& Value)` |
| **Lua 调用** | `Mession.GetGlobal("counter")` / `Mession.SetGlobal("counter", 42)` |
| **映射** | 走 `IScriptEngine::Get/SetGlobal`(已在 LuaEngine 实现) |

### 3.4 `Mession.RegisterModule(name, proxy_table)`(框架侧)

| | |
|---|---|
| **签名** | `static void RegisterModule(MStringView Name, MClass* Cls)` |
| **Lua 调用** | 由 `MLuaModule::InstallIntoGlobal` 调用,不在业务脚本直接暴露 |
| **作用** | 把 MClass 反射 proxy 挂到 `Mession[Name]` 上,业务侧可访问 |

---

## 4. 实现要点

### 4.1 MClassProxy metatable

```lua
-- C++ 端在 Initialize 时 push 这个 metatable 到注册表
local MClassProxy = {
    __index = function(t, key)
        -- 走反射查 MClass 的字段/方法/属性
        local cls = rawget(t, "__cls__")
        local k = tostring(key)
        -- 先查 property,再查 function
        local prop = cls:FindProperty(k)
        if prop then return cls:GetProperty(prop) end
        local fn = cls:FindFunction(k)
        if fn then
            return function(self_, ...)
                return Mession.InvokeClass(cls.Name, k, ...)
            end
        end
        return nil
    end,
    __newindex = function(t, key, value)
        local cls = rawget(t, "__cls__")
        cls:SetProperty(tostring(key), value)
    end,
    __tostring = function(t)
        return rawget(t, "__cls__").Name
    end
}
```

### 4.2 数据流(Lua 调 C++)

```
Lua:  Mession.InvokeClass("MEchoService", "Echo", "Hello")
              │
              ▼  (C function mth_InvokeClass)
C++:   1. 拿 MClass("MEchoService") via MObject::FindClass
      2. MClass::FindFunction("Echo")
      3. BuildScriptArgs({"Hello"})
      4. CallFunction(Fn, Args) → SFutureResult
      5. lua_resume + push result
              │
              ▼
Lua:  local r = pcall(Mession.InvokeClass, "MEchoService", "Echo", "Hello")
```

### 4.3 错误传递

| C++ → Lua | Lua 看到 |
|---|---|
| `TResult::Err(MString)` | `(nil, "[error_code] message")` |
| `TResult::Ok(value)` | `(value, nil)` |
| SFutureResult pending | 阻塞 lua_resume,完成时 resume |
| 异常 | `lua_error` → pcall 捕获 |

---

## 5. 测试要求

`TestLuaReflectBridge.cpp` 覆盖:

1. `TestFindClass`:`Mession.FindClass("MEchoService")` 返回非 nil metatable;不存在的类返回 nil
2. `TestInvokeClassEcho`:同步调用,验证返回值
3. `TestInvokeClassNotFound`:方法不存在 → pcall 捕获错误
4. `TestGetSetGlobal`:`SetGlobal("x", 42) → GetGlobal("x") == 42`
5. `TestGlobalNotFound`:`GetGlobal("nonexistent")` 返回 nil

---

## 6. 验收标准

- ✅ 业务侧能在 `.tl` / `.lua` 文件用 `Mession.FindClass` / `InvokeClass` / `Get/SetGlobal`
- ✅ 类不存在 / 方法不存在走 pcall 错误路径,不抛 Lua panic
- ✅ 同步调用返回 `(ok, nil)`,错误返回 `(nil, errstr)`
- ✅ 与现有 `IScriptEngine` 接口兼容(走 `CallFunction / Get/SetGlobal`)
- ✅ `MHeaderTool LuaBindEmitter` emit 的 `M.InvokeClass` 桥接代码能落地(LuaBindEmitter Task 7 已 emit,本 spec 落地运行时桥)

---

## 7. 风险与降级

| 风险 | 降级 |
|---|---|
| `MFunction::Invoke` 路径性能瓶颈(反射调用) | 缓存 FunctionId → mth_<fn> lua_pushcfunction 映射 |
| 标量类型桥接精度丢失(int64 ↔ double) | 桥接层显式 `lua_Integer` 强转 + 范围断言 |
| Lua GC 与 MObject 生命周期脱钩 | 沿用 `object-lifecycle.md` 决策:**只持 ActorId**,不持对象引用 |
| Lua 异常路径丢失 C++ 堆栈 | `WrapException` 抽 `luaL_traceback` + C++ 帧混合栈 |

---

## 8. 工作量估算

| 子任务 | 工作量 |
|---|---|
| `MLuaReflectBridge.h/.cpp` + metatable | 1 人日 |
| `reflect_init.lua`(Mession.* 注入) | 0.5 人日 |
| `TestLuaReflectBridge.cpp` 5 个测试 | 0.5 人日 |
| 接入 `LuaEngine::Initialize` + `LuaModule::InstallIntoGlobal` | 0.5 人日 |
| **合计** | **2.5 人日** |

---

## 9. 关联文档

- 抽象层:`2026-08-04-script-engine-abstract.md`(IScriptEngine 接口)
- 权威基线:`2026-08-04-object-lifecycle.md`(业务侧不持对象)
- Lua 嵌入:`2026-08-05-lua-impl.md`(Lua 5.4 + Teal 总 plan)
- MHeaderTool LuaBindEmitter:`2026-08-07-.../mheadercodegen-...`
- 后续 4 份 spec:`2026-08-07-lua-vector-bridge.md` / `...-map-bridge.md` / `...-log-bridge.md` / `...-rpc-bridge.md`