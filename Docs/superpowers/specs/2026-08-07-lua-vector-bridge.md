# Lua-C++ Vector 桥 Spec(分册 2/5)

日期:2026-08-07
作者:标准库桥接 spec 2/5
状态:DRAFT
范围:把 C++ `TVector<T>` (即 `std::vector<T>`)桥到 Lua 5.4 userdata,业务侧能用 `Mession.Vector` 工厂 + 索引 / push / pop / insert / remove / iter。

---

## 1. 总览

| 维度 | 决策 |
|---|---|
| 桥接风格 | **userdata + metatable**,值类型用 `int64 / double / MString`(同 MClass 字段类型) |
| 生命周期 | `__gc` 释放 C++ `TVector`,Lua GC 触发 |
| 内存所有权 | userdata 直接持有 `TVector*`,Lua GC 时 `delete` |
| 类型映射 | Lua 侧元素类型只支持 `integer / number / string` 三种标量;复杂类型按"指针 + ActorId"规则(后续 spec) |

业务侧心智:

```lua
-- 创建
local v = Mession.Vector.new()
local v2 = Mession.Vector.new(3, "hello")  -- size=3, init="hello"

-- 索引(1-based)
print(v:get(1))                          -- 读
v:set(1, 42)                             -- 写
v:push(99)                               -- 追加
local x = v:pop()                        -- 弹尾

-- 长度与信息
print(v:size())
print(#v)                                 -- __len 元方法

-- 操作
v:insert(2, 55)
v:remove(1)

-- 迭代(标准 ipairs)
for i, v in ipairs(v) do print(i, v) end

-- 拷贝(显式,避免共享同一 userdata)
local v3 = v:clone()
```

---

## 2. 依赖与文件结构

新增:
```
Source/Common/Script/Lua/
├── MLuaVector.h                          # Vector bridge
├── MLuaVector.cpp
├── Resources/
│   └── vector_init.lua                   # Mession.Vector.new 工厂
└── Tests/
    └── TestLuaVector.cpp
```

修改:
```
Source/Common/Script/Lua/CMakeLists.txt
Source/Common/Script/Lua/MLuaReflectBridge.cpp   # 反射桥调用 MLuaVector::Install(L)
```

---

## 3. API 详细

### 3.1 `Mession.Vector.new(size?, init?)`

| | |
|---|---|
| **Lua 签名** | `Mession.Vector.new([size: integer [, init: any]])` |
| **C++ 实现** | `lua_newuserdata(L, sizeof(TVector<int64>))` + metatable `MVectorProxy` |
| **默认值** | `size=0, init=nil`(`init=nil` 走 `value-initialize`) |

### 3.2 instance methods

| 方法 | 签名 | 行为 |
|---|---|---|
| `:get(i)` | `(integer) -> any` | 1-based;越界返回 `nil, out_of_range` |
| `:set(i, v)` | `(integer, any) -> nil` | 1-based;越界返回 `nil, out_of_range` |
| `:push(v)` | `(any) -> nil` | 追加 |
| `:pop()` | `() -> any` | 弹尾;空时返回 `nil, empty` |
| `:insert(i, v)` | `(integer, any) -> nil` | 1-based;`i=size+1` 等价于 push |
| `:remove(i)` | `(integer) -> any` | 移除并返回元素 |
| `:size()` | `() -> integer` | 等价 `#v` |
| `:clear()` | `() -> nil` | 清空 |
| `:clone()` | `() -> Mession.Vector` | 深拷贝 |

### 3.3 metamethods

| 元方法 | 行为 |
|---|---|
| `__index(t, i)` | 越界返回 nil,否则用 `tv[i-1]`;**优先**于 `:get` |
| `__newindex(t, i, v)` | `tv[i-1] = v`;越界 throw |
| `__len(t)` | `return #tv` |
| `__pairs(t)` | 返回 `ipairs-style` 迭代器 |
| `__gc(t)` | `delete t` (释放 TVector) |
| `__tostring(t)` | `"Vector(size=N)"` |
| `__eq(a, b)` | 长度 + 元素全等 |

---

## 4. 实现要点

### 4.1 userdata + metatable

```cpp
// C++ 端
class MLuaVector {
public:
    static void Install(lua_State* L);                     // 注册 MVectorProxy metatable
    static int  New(lua_State* L);                        // factory

private:
    static int  Get(lua_State* L);
    static int  Set(lua_State* L);
    static int  Push(lua_State* L);
    // ... 其它 instance methods
};
```

### 4.2 标量转换

| Lua 栈类型 | TVector 元素类型 |
|---|---|
| `LUA_TNUMBER`(整数) | `int64` |
| `LUA_TNUMBER`(浮点) | `double`(与 `int64` 共存;`set` 时不自动转型) |
| `LUA_TSTRING` | `MString` |
| `LUA_TBOOLEAN` | 拒绝(返回 nil, type_error) |
| 其他 | 拒绝 |

具体类型推断在 `:get` 时返回 **Lua number**(统一是 `lua_Number` = `double`)或 **Lua string**;`:set` 时按栈类型决定 C++ 类型。

### 4.3 metatable 注册

```lua
-- MVectorProxy 注册到 LUA_REGISTRYINDEX["MVectorProxy"]
MVectorProxy = {
    __index = function(t, k)
        if type(k) == "number" then return rawget_vector_at(t, k) end
        -- 否则转发到 method lookup
        return MVectorProxy_methods[k]
    end,
    __newindex = function(t, k, v)
        if type(k) == "number" then
            rawset_vector_at(t, k, v)
            return
        end
        error("cannot set non-index field")
    end,
    __len = function(t) return #rawget(t, "__vec__") end,
    __gc = function(t) rawset(t, "__vec__", nil) end,
}
```

### 4.4 factory 注册

```lua
-- Lua 端在 reflect_init.lua 末尾注入
Mession.Vector = {
    new = function(size, init)
        local v = MVector_New()
        if size and size > 0 then
            for i = 1, size do v:push(init) end
        end
        return v
    end,
}
```

---

## 5. 测试要求

`TestLuaVector.cpp` 覆盖:

1. `TestCreateEmpty`:`Mession.Vector.new()` 后 `size() == 0`
2. `TestCreateWithSize`:`Vector.new(3, 0)` 后 `size() == 3` 且每元素是 0
3. `TestPushPop`:`push/pop` 顺序对,FIFO/LIFO 正确
4. `TestInsertRemove`:中间位置正确插入 / 移除
5. `TestGetSetBoundary`:越界 `:get` 返回 nil;越界 `:set` 抛错
6. `TestClone`:克隆后修改克隆不影响原 vector
7. `TestIpairs`:标准 `ipairs` 迭代顺序正确
8. `TestGC`:`collectgarbage` 后 userdata 不再可访问(C++ 析构已调)

---

## 6. 验收标准

- ✅ `Mession.Vector.new` 创建 userdata,`__gc` 释放 TVector
- ✅ 所有 instance methods 在 Lua 端可调
- ✅ 元方法(`__index / __len / __gc / __pairs / __tostring`)全部实现
- ✅ `ipairs(v)` 正确迭代
- ✅ 标量类型双向转换无精度丢失
- ✅ 越界 / 类型错误走 pcall 错误路径,不 panic

---

## 7. 风险与降级

| 风险 | 降级 |
|---|---|
| `TVector<T>` 模板类型擦除(metatable 不能 per-T) | **统一 element type**:`int64 + double + MString` 三合一 `TVariant` 风格(不是真正 TVariant,是 union) |
| userdata 跨脚本 release 后 C++ 资源仍被引用 | `__gc` 严格释放;不允许 long-lived Lua 引用,过 VSC_DEBUG 单元测 |
| `__index` 转发到 method table 与 numeric index 冲突 | numeric index 用 `lua_isnumber` 判,其它走 method |
| Lua GC 与 C++ 析构时机差 | Lua 5.4 的 `__close` + `toclose` 元方法兜底 |

---

## 8. 工作量估算

| 子任务 | 工作量 |
|---|---|
| `MLuaVector.h/.cpp` + metatable | 1.5 人日 |
| `vector_init.lua` | 0.5 人日 |
| `TestLuaVector.cpp` 8 个测试 | 0.5 人日 |
| 接入 `MLuaReflectBridge::Install` | 0.25 人日 |
| **合计** | **2.75 人日** |

---

## 9. 关联文档

- 反射桥:`2026-08-07-lua-reflect-bridge.md`(调用入口)
- 抽象层:`2026-08-04-script-engine-abstract.md`
- 业务侧不持对象:`2026-08-04-object-lifecycle.md`
- Lua 嵌入:`2026-08-05-lua-impl.md`
- 后续:`2026-08-07-lua-map-bridge.md`