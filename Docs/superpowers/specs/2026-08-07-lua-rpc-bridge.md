# Lua-C++ rpc / id / time 桥 Spec(分册 5/5)

日期:2026-08-07
作者:标准库桥接 spec 5/5
状态:DRAFT
范围:把 C++ `MRpcChannel`、`MUniqueIdGenerator`、`MTime` 桥到 Lua 5.4,业务侧能用 `Mession.rpc.call(...)`、`Mession.uniqueId()`、`Mession.time.now()`。

---

## 1. 总览

| 维度 | 决策 |
|---|---|
| 桥接风格 | `lua_pushcfunction` 直接调 C++ API |
| rpc 异步 | 走现有 `SFutureResult<T>`,Lua 侧 `await` 或回调 |
| id | `uint64`,返回 Lua number(双精度 → 精度范围 ≤ 2^53) |
| time | `int64` 毫秒,Lua number 同样精度限制 |

业务侧心智:

```lua
-- RPC(同步,阻塞到结果)
local resp, err = Mession.rpc.call("MEchoService", "Echo", "hello")
if resp then print("ok:", resp) else print("err:", err) end

-- RPC(异步,callback)
Mession.rpc.call_async("MEchoService", "Echo", "hello", function(resp, err)
    if resp then print("ok:", resp) else print("err:", err) end
end)

-- UniqueId
local id = Mession.uniqueId()  -- 警告:id > 2^53 精度丢失
print(id)

-- Time
local now_ms = Mession.time.now()
Mession.time.sleep(100)         -- 阻塞 100ms(不推荐在游戏循环)
```

---

## 2. 依赖与文件结构

新增:
```
Source/Common/Script/Lua/
├── MLuaRpcBridge.h                        # rpc + id + time bridge
├── MLuaRpcBridge.cpp
├── Resources/
│   └── rpc_init.lua                       # Mession.rpc / uniqueId / time
└── Tests/
    └── TestLuaRpcBridge.cpp
```

修改:
```
Source/Common/Script/Lua/CMakeLists.txt
Source/Common/Script/Lua/MLuaReflectBridge.cpp
```

---

## 3. API 详细

### 3.1 `Mession.rpc.call(class, method, args...)`

| | |
|---|---|
| **签名** | `rpc.call(string class, string method, any...) -> value, error` |
| **C++ 实现** | `MRpcChannel::Call<Resp>(EServerType, "Class", "Method", TScriptArgs)` |
| **Lua 调用** | `local r, err = Mession.rpc.call("MEchoService", "Echo", "hello")` |
| **返回值** | `(value, nil)` 成功 / `(nil, errstr)` 失败 |
| **阻塞** | lua_yield 到 C++ SFutureResult 完成 |

### 3.2 `Mession.rpc.call_async(class, method, args, callback)`

| | |
|---|---|
| **签名** | `rpc.call_async(string, string, any..., function)` |
| **C++ 实现** | 同 `call` 但不 yield,SFutureResult 完成时调 callback |
| **Lua 调用** | `Mession.rpc.call_async("MEchoService", "Echo", "hi", function(r, err) ... end)` |
| **限制** | 业务侧必须能管理 callback 生命周期,不能泄漏 |

### 3.3 `Mession.uniqueId()`

| | |
|---|---|
| **签名** | `uniqueId() -> number` |
| **C++ 实现** | `MUniqueIdGenerator::Generate()`(返回 `uint64`) |
| **Lua 调用** | `local id = Mession.uniqueId()` |
| **限制** | Lua number 是 double,`uint64 > 2^53` 精度丢失 |
| **建议** | ActorId 控制在 `2^53` 范围内;否则走字符串 id |

### 3.4 `Mession.time.now()`

| | |
|---|---|
| **签名** | `time.now() -> integer` |
| **C++ 实现** | `std::chrono::duration_cast<ms>(steady_clock::now()).count()` |
| **Lua 调用** | `local now_ms = Mession.time.now()` |

### 3.5 `Mession.time.sleep(ms)`

| | |
|---|---|
| **签名** | `time.sleep(integer)` |
| **C++ 实现** | `std::this_thread::sleep_for(ms)` |
| **限制** | **会阻塞 Lua 协程**,不推荐在游戏循环用 |

---

## 4. 实现要点

### 4.1 RPC 同步调用

```cpp
static int RpcCall(lua_State* L) {
    // Stack: class, method, arg1, arg2, ...
    const char* ClassName = luaL_checkstring(L, 1);
    const char* Method    = luaL_checkstring(L, 2);
    int NArgs = lua_gettop(L) - 2;
    
    // 1. 找 ServerType(走 MObject::FindClass 的 ServerType)
    MClass* Cls = MObject::FindClass(ClassName);
    if (!Cls) return luaL_error(L, "class not found");
    
    // 2. 找 Function
    MFunctionObject* Fn = Cls->FindFunctionByName(Method);
    if (!Fn) return luaL_error(L, "method not found");
    
    // 3. Build ScriptArgs from stack
    TScriptArgs Args = BuildArgsFromStack(L, 3, NArgs);
    
    // 4. 同步等待 SFutureResult(可走 lua_yield)
    auto Fut = MRpcChannel::Get().CallServerFunction(Cls, Fn, Args);
    Fut.Wait();   // 简化:阻塞;真实应 yield
    
    // 5. Push result
    auto R = Fut.Get();
    if (R.IsErr()) {
        lua_pushnil(L);
        lua_pushstring(L, R.GetError().c_str());
        return 2;
    }
    lua_pushvalue(L, ?);  // 占位
    lua_pushnil(L);
    return 2;
}
```

### 4.2 uniqueId 精度警告

```cpp
static int UniqueId(lua_State* L) {
    uint64 Id = MUniqueIdGenerator::Generate();
    if (Id > (uint64(1) << 53)) {
        LOG_WARN("MUniqueId > 2^53, Lua precision loss: %llu", Id);
    }
    lua_pushnumber(L, static_cast<double>(Id));
    return 1;
}
```

---

## 5. 测试要求

`TestLuaRpcBridge.cpp` 覆盖:

1. `TestUniqueId`:`uniqueId()` 返回 number,连续两次值不同
2. `TestUniqueIdSequence`:同进程连续调用 id 递增
3. `TestTimeNow`:`time.now()` 返回接近当前时间(允许 ±1s)
4. `TestTimeSleep`:`time.sleep(50)` 后 `time.now()` 至少增加 50ms
5. `TestRpcCallEcho`:实际 EchoService 跑着,`rpc.call("MEchoService", "Echo", "hi")` 返回字符串
6. `TestRpcCallClassNotFound`:`rpc.call("Nonexistent", "Echo")` 返回 nil + error
7. `TestRpcCallMethodNotFound`:类存在但方法不存在 → nil + error

> 测试需要 EchoService 进程跑着(`Scripts/servers.py start`)

---

## 6. 验收标准

- ✅ 同步 RPC 调用成功返回结果
- ✅ 异步 RPC 通过 callback 拿到结果(不阻塞 Lua)
- ✅ uniqueId 正常返回连续 id
- ✅ time.now / time.sleep 正常
- ✅ RPC 错误路径走 pcall,不 panic

---

## 7. 风险与降级

| 风险 | 降级 |
|---|---|
| uniqueId 精度丢失(>2^53) | 业务侧用 string id;ActorId 系统设计保留这个限制 |
| rpc 同步阻塞 Lua 协程 | 默认走 callback 异步;同步只在测试 / 调试用 |
| time.sleep 在主线程 = 阻塞 game loop | 文档明确警告;推荐 Lua 协程 + C++ Timer |
| 回调 closure 跨脚本 release | `call_async` 内部 weak ref 跟踪 Lua registry |
| RPC 跨进程时本机不注册 ClientApi | 走 MClientManifest,本 spec **只覆盖已注册 manifest 的函数** |

---

## 8. 工作量估算

| 子任务 | 工作量 |
|---|---|
| `MLuaRpcBridge.h/.cpp` rpc + uniqueId + time | 1.5 人日 |
| `rpc_init.lua` | 0.5 人日 |
| `TestLuaRpcBridge.cpp` 7 个测试 | 1 人日(依赖真实服务) |
| 接入 | 0.25 人日 |
| **合计** | **3.25 人日** |

---

## 9. 关联文档

- 反射桥:`2026-08-07-lua-reflect-bridge.md`
- 抽象层 IScriptEngine.CallFunction:`2026-08-04-script-engine-abstract.md`
- MRpcChannel:`Source/Common/Net/Rpc/MRpcChannel.h`
- MUniqueIdGenerator:`Source/Common/Runtime/Id.h`
- MTime:`Source/Common/Runtime/Time.h`

---

## 附录:5 份 spec 全局落地策略

按依赖顺序落地:

1. **反射桥**(基础,先做) → Vector → Map → log/format → rpc
2. 每份一个独立 plan,走 writing-plans skill
3. 每份一份 commit(commit message 沿用已有风格)
4. 总工作量:2.5 + 2.75 + 2.75 + 2 + 3.25 = **13.25 人日**
5. 单人约 3-4 周完成
6. 每份 spec 配套独立 plan,可暂停在任意点