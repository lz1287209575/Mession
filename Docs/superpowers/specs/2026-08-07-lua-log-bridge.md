# Lua-C++ log/format 桥 Spec(分册 4/5)

日期:2026-08-07
作者:标准库桥接 spec 4/5
状态:DRAFT
范围:把 Mession 的日志系统 `MLog` 和格式化工具 `MFormat` 桥到 Lua 5.4,业务侧能用 `Mession.log.{debug,info,warn,error}` 与 `Mession.format(fmt, args...)`。

---

## 1. 总览

| 维度 | 决策 |
|---|---|
| 桥接风格 | `lua_pushcfunction` 直接调 C++ API |
| Log 目标 | `MLog` 已支持 category(`LogCore / LogNet / ...`),桥接到 `Mession.log.core.{debug,...}` |
| Format | `MFormat::Format(fmt, ...)`(底层 `fmt::format`) |
| 错误处理 | `MLog::Write` 不抛异常;`MFormat::Format` 抛时 lua_error + pcall 捕获 |

业务侧心智:

```lua
-- log
Mession.log.core.debug("hello %s", "world")     -- C++ format string
Mession.log.net.warn("connection lost")
Mession.log.rpc.error("call failed: %d", errcode)

-- format
local s = Mession.format("player %s level %d", "alice", 10)
local path = Mession.format("%s/%s.bin", dir, filename)
```

---

## 2. 依赖与文件结构

新增:
```
Source/Common/Script/Lua/
├── MLuaLogBridge.h                        # log + format bridge
├── MLuaLogBridge.cpp
├── Resources/
│   └── log_init.lua                       # Mession.log / Mession.format
└── Tests/
    └── TestLuaLogBridge.cpp
```

修改:
```
Source/Common/Script/Lua/CMakeLists.txt
Source/Common/Script/Lua/MLuaReflectBridge.cpp
```

---

## 3. API 详细

### 3.1 `Mession.log.{category}.{level}`

| category | 含义 | 来源 |
|---|---|---|
| `core` | 默认 / 核心 | `LogCore` |
| `net` | 网络 | `LogNet` |
| `rpc` | RPC | `LogRpc` |
| `db` | 数据库 | `LogDb` |
| `auth` | 鉴权 | `LogAuth` |
| `scene` | 场景 | `LogScene` |

| level | 含义 |
|---|---|
| `debug` | `ELogLevel::Debug` |
| `info` | `ELogLevel::Info` |
| `warn` | `ELogLevel::Warn` |
| `error` | `ELogLevel::Error` |
| `fatal` | `ELogLevel::Fatal` |

| | |
|---|---|
| **C++ 实现** | `MLog::Write(LogCategory, Level, Format, ...)` |
| **Lua 调用** | `Mession.log.core.info(fmt, ...)` |
| **返回值** | 无(只写 log) |
| **失败处理** | 格式错误 → 仍写一条 "log format error" 错误行 |

### 3.2 `Mession.format(fmt, ...)`

| | |
|---|---|
| **签名** | `string format(string fmt, any ...)` |
| **C++ 实现** | `MFormat::Format(fmt, args)` |
| **Lua 调用** | `local s = Mession.format("hello %s", "world")` |
| **格式串** | `fmt::format` 语法(`{}`、`{:06d}` 等) |
| **返回值** | 字符串 |
| **失败处理** | `Format` 抛 → `lua_error` → pcall 捕获 |

---

## 4. 实现要点

### 4.1 反射注册

```cpp
// MLuaLogBridge::Install
static void Install(lua_State* L) {
    // Mession.log = { core = { debug=..., info=..., ... }, net = {...}, ... }
    lua_newtable(L);
    for (auto& [Name, LogCategory] : MLog::GetCategories()) {
        lua_newtable(L);                                              // category table
        for (auto Level : Levels) {
            // debug/info/warn/error/fatal cfunction 调 MLog::Write(LogCategory, Level, fmt, ...)
        }
        lua_setfield(L, -2, Name.c_str());
    }
    lua_setfield(L, -2, "log");
    
    // Mession.format = function(fmt, ...) -> string
    lua_pushcfunction(L, &MLuaLogBridge::Format);
    lua_setfield(L, -2, "format");
}
```

### 4.2 Log cfunction

```cpp
static int LogCoreDebug(lua_State* L) {
    // Stack: fmt, arg1, arg2, ...
    const char* Fmt = luaL_checkstring(L, 1);
    // 透传 fmt + args 给 MLog::Write,用 lua_gettop 取参数个数
    MLog::Write(LogCore, ELogLevel::Debug, Fmt);
    return 0;
}
```

**参数透传策略**:
- 简单情况:仅 fmt,直接调
- 复杂情况(fmt + 多 args):走 `fmt::format` 内部格式串

### 4.3 Format 函数

```cpp
static int Format(lua_State* L) {
    const char* Fmt = luaL_checkstring(L, 1);
    try {
        MString Result = MFormat::Format(Fmt);     // 单参数简化版
        lua_pushstring(L, Result.c_str());
        return 1;
    } catch (const std::exception& e) {
        return luaL_error(L, "format error: %s", e.what());
    }
}
```

> **关于 var args**:`MFormat::Format` 当前签名只接 `(fmt)`,不支持 var args(参考 `2026-08-03-standard-library-gap.md` §"MFormat + MStringBuilder")。**本 spec 不引入 var args**,业务侧需手动拼接 `string.format` 或多次 format。

---

## 5. 测试要求

`TestLuaLogBridge.cpp` 覆盖:

1. `TestLogCoreInfo`:调 `Mession.log.core.info("hello %s", "world")`,无异常
2. `TestLogRpcError`:调 `Mession.log.rpc.error(...)`,无异常
3. `TestLogAllLevels`:`debug/info/warn/error/fatal` 全部能调
4. `TestFormatSimple`:`Mession.format("hello %s", "world")` 返回 "hello world"
5. `TestFormatNumber`:`Mession.format("%06d", 42)` 返回 "000042"
6. `TestFormatError`:`Mession.format("%s%d", "abc")` (类型不匹配) → pcall 捕获

> log 实际写文件不验证,只验证不崩溃

---

## 6. 验收标准

- ✅ 6 个 category × 5 个 level = 30 个 log 入口
- ✅ `Mession.format` 简单 fmt 可用
- ✅ 错误格式走 pcall,不 panic
- ✅ 不影响现有 log 性能(走 `MLog::Write` 直通)

---

## 7. 风险与降级

| 风险 | 降级 |
|---|---|
| `MFormat::Format` 不支持 var args | 业务侧用 `string.format` 替代;本 spec **明确不实现** var args |
| Log 写盘失败 → Lua 死锁 | `MLog::Write` 已异步(`ring + dispatcher + sinks`);不阻塞 Lua |
| 日志级别过滤(debug 不输出) | 透传 `MLog` 行为;`MESSION_LOG_LEVEL=Debug` 等 env 控制 |
| Log 性能(Lua 每次 cfunction 调 C++) | 缓存 log function id,业务侧按需缓存 |

---

## 8. 工作量估算

| 子任务 | 工作量 |
|---|---|
| `MLuaLogBridge.h/.cpp` 30 个 log + format | 1 人日 |
| `log_init.lua` | 0.25 人日 |
| `TestLuaLogBridge.cpp` 6 个测试 | 0.5 人日 |
| 接入 | 0.25 人日 |
| **合计** | **2 人日** |

---

## 9. 关联文档

- 反射桥:`2026-08-07-lua-reflect-bridge.md`
- Log 模块:`Source/Common/Runtime/Log/Log.h`
- MFormat:`Source/Common/Runtime/StringUtils.h`(已有占位)
- 后续:`2026-08-07-lua-rpc-bridge.md`