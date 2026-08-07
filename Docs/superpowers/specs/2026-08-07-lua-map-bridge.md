# Lua-C++ Map 桥 Spec(分册 3/5)

日期:2026-08-07
作者:标准库桥接 spec 3/5
状态:DRAFT
范围:把 C++ `TMap<K,V>` (即 `std::unordered_map<K,V>`)桥到 Lua 5.4 userdata,业务侧能用 `Mession.Map` 工厂 + get / set / delete / iter。

---

## 1. 总览

| 维度 | 决策 |
|---|---|
| 桥接风格 | **userdata + metatable** + `__pairs` 迭代 |
| Key 类型 | Lua `string`(全 key 都转 MString) |
| Value 类型 | 与 Vector 元素相同:`int64 / double / MString` |
| 生命周期 | `__gc` 释放 C++ `TMap<MString, MScalarValue>` |
| 内存所有权 | userdata 直接持有 `TMap*`,Lua GC 时 `delete` |

业务侧心智:

```lua
-- 创建
local m = Mession.Map.new()
m:set("name", "alice")
m:set("hp", 100)
m:set("active", true)  -- bool 拒绝,转 string "true"

-- 查询
print(m:get("name"))                    -- "alice"
print(m:get("missing", "default"))       -- nil OR default value

-- 删除
m:delete("hp")

-- 元信息
print(m:size())
print(m:contains("name"))               -- true / false

-- keys / values
local ks = m:keys()                     -- 返回 Vector-like table
local vs = m:values()

-- 迭代(标准 pairs)
for k, v in pairs(m) do print(k, v) end

-- 拷贝
local m2 = m:clone()
```

---

## 2. 依赖与文件结构

新增:
```
Source/Common/Script/Lua/
├── MLuaMap.h                             # Map bridge
├── MLuaMap.cpp
├── Resources/
│   └── map_init.lua                      # Mession.Map.new
└── Tests/
    └── TestLuaMap.cpp
```

修改:
```
Source/Common/Script/Lua/CMakeLists.txt
Source/Common/Script/Lua/MLuaReflectBridge.cpp   # 调用 MLuaMap::Install
```

---

## 3. API 详细

### 3.1 `Mession.Map.new([size_hint])`

| | |
|---|---|
| **签名** | `Mession.Map.new([size_hint: integer])` |
| **C++ 实现** | `lua_newuserdata(L, sizeof(TMap<MString, MScalarValue>))` + metatable `MMapProxy` |
| **参数** | `size_hint` 仅作预留桶数提示(转 `reserve`) |

### 3.2 instance methods

| 方法 | 签名 | 行为 |
|---|---|---|
| `:get(k)` | `(string) -> any` | 不存在 → `nil` |
| `:get(k, default)` | `(string, any) -> any` | 不存在 → 返回 default |
| `:set(k, v)` | `(string, any) -> nil` | key 必须 string;value 类型受支持 |
| `:delete(k)` | `(string) -> boolean` | true=删了,false=本来就没有 |
| `:size()` | `() -> integer` | |
| `:clear()` | `() -> nil` | |
| `:contains(k)` | `(string) -> boolean` | |
| `:keys()` | `() -> table(vector-like)` | 返回 Vector-spec 同款 userdata |
| `:values()` | `() -> table(vector-like)` | 同上 |
| `:clone()` | `() -> Mession.Map` | 深拷贝 |

### 3.3 metamethods

| 元方法 | 行为 |
|---|---|
| `__index(t, k)` | `t:get(k)`(等同 dict) |
| `__newindex(t, k, v)` | `t:set(k, v)` |
| `__pairs(t)` | 标准 pairs 迭代器 |
| `__len(t)` | `t:size()` |
| `__gc(t)` | `delete t` |
| `__tostring(t)` | `"Map(size=N)"` |
| `__eq(a, b)` | 长度 + key/value 全等 |

---

## 4. 实现要点

### 4.1 标量 Value 类型

`MScalarValue` 是 `union { int64 Int; double Double; MString String; }` + `enum class Type`。每次 `:set` 决定类型,`:get` 按 Type 转 Lua 值:

```cpp
enum class MScalarType : uint8 { Int, Double, String };

struct MScalarValue {
    MScalarType Type = MScalarType::Int;
    int64       IntVal    = 0;
    double      DoubleVal = 0.0;
    MString     StringVal;

    static MScalarValue FromLua(lua_State* L, int idx);
    void PushToLua(lua_State* L) const;
};
```

### 4.2 metatable 注册

```lua
MMapProxy = {
    __index = function(t, k)
        return rawget_map_get(t, k)
    end,
    __newindex = function(t, k, v)
        rawset_map_set(t, k, v)
    end,
    __pairs = function(t)
        -- 返回迭代函数 + t + nil
        local map = rawget(t, "__map__")
        local keys = {}
        for k in pairs(map) do table.insert(keys, k) end
        local i = 0
        return function()
            i = i + 1
            if keys[i] then return keys[i], map[keys[i]] end
        end
    end,
    __gc = function(t) rawset(t, "__map__", nil) end,
}
```

### 4.3 factory

```lua
Mession.Map = {
    new = function(size_hint)
        return MMap_New(size_hint or 0)
    end,
}
```

---

## 5. 测试要求

`TestLuaMap.cpp` 覆盖:

1. `TestCreateEmpty`:`Map.new()` 后 `size() == 0`
2. `TestSetGet`:基本 set / get
3. `TestGetDefault`:不存在 key + default → 返回 default
4. `TestDelete`:删除存在/不存在 key
5. `TestContains`:exists / missing
6. `TestKeysValues`:返回长度一致
7. `TestPairsIter`:标准 `pairs(m)` 完整迭代
8. `TestClone`:克隆后修改互不影响
9. `TestTypeError`:key 是 number(非 string)→ 抛错
10. `TestGC`:`collectgarbage` 后析构已调

---

## 6. 验收标准

- ✅ `Mession.Map.new` 创建 userdata,`__gc` 释放 TMap
- ✅ 所有 instance methods 可调
- ✅ `pairs(m)` 完整迭代
- ✅ key 是 string,value 三种标量(拒绝 bool / table / nil)
- ✅ 深拷贝独立
- ✅ 越界 / 类型错误走 pcall

---

## 7. 风险与降级

| 风险 | 降级 |
|---|---|
| `pairs` 迭代期间 C++ 侧修改 Map | 沿用 Vector 方案:迭代前快照 keys,迭代期间不改 |
| Key 非 string 在 Lua 是常见错 | 显式 `type(k) ~= "string"` 检查,报错清晰 |
| Map 大时 keys() 返回巨大 Vector | 限制:单次 keys() 上限 65536,超过报 "too large" |
| nested Map / Vector 作为 Value | 本 spec **不支持**,Value 只支持标量;嵌套需求推到 spec 6/5 |

---

## 8. 工作量估算

| 子任务 | 工作量 |
|---|---|
| `MLuaMap.h/.cpp` + metatable + scalar 转换 | 1.5 人日 |
| `map_init.lua` | 0.5 人日 |
| `TestLuaMap.cpp` 10 个测试 | 0.5 人日 |
| 接入 `MLuaReflectBridge::Install` | 0.25 人日 |
| **合计** | **2.75 人日** |

---

## 9. 关联文档

- 反射桥:`2026-08-07-lua-reflect-bridge.md`(注册入口)
- Vector 桥:`2026-08-07-lua-vector-bridge.md`(MScalarValue / userdata 模式共享)
- 抽象层:`2026-08-04-script-engine-abstract.md`
- 后续:`2026-08-07-lua-log-bridge.md`