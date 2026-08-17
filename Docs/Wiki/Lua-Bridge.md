# Lua 脚本桥(Lua-Bridge)

> Mession 的 Lua 5.4 集成与 C++ 标准库桥接。架构分层:抽象层 `IScriptEngine` → `MLuaEngine`(Lua 引擎实现)→ `MLuaScriptState`(Lua 虚拟机状态),业务侧通过 `Mession.*` 全局命名空间使用。

## Lua 引擎集成

### MLuaScriptState(虚拟机状态)

`Source/Common/Script/Lua/LuaScriptState.h`

- 持有 `lua_State* L`,每个状态一个独立 VM。
- `LoadBuffer(Name, Bytes, Size)`:加载并执行 Lua 字节流;返回 `Err("")` 表示成功。
- `GetLuaState()` / `IsValid()`:取原生 state、检查可用性。
- 禁止拷贝赋值;析构释放 `lua_State`。

### MLuaEngine(引擎实现)

`Source/Common/Script/Lua/LuaEngine.h` / `LuaEngine.cpp`,实现抽象层 `IScriptEngine`:

| 能力 | 说明 |
|---|---|
| 生命周期 | `Initialize` 创建 `MLuaScriptState`;`Shutdown` 清理模块与状态;`Tick` / `StepCoroutines` 驱动帧与协程 |
| 加载 | `LoadBufferIntoState` 向当前 State 加载字节流;`Reload(EReloadMode)` + `ReplaceState` 支持热重载(旧 State 由调用方负责 `lua_close`) |
| 调用 | `CallFunction(MFunctionObject*, Args)` / `CallFunctionById`(按反射 FunctionId 查类调用,当前简化支持无参/全标量参数,原生调 `NativeInvoke`) |
| 全局 | `SetGlobal` / `GetGlobal(MStringView)` — 走 `TVariant`(Null/Bool/Int/Double/String)↔ Lua 值双向转换 |
| 对象 | `CreateInstance` / `CreateActor`(生成 ActorId 并注册到 `MActorRouter`)/ `CreateModule` |
| 实例句柄 | `CreateInstanceByClassName` / `InvokeInstanceMethod` / `ReleaseInstance`(见下节) |
| 语言标识 | `GetLanguage() == EScriptLanguage::Lua`;默认**不沙箱** |

### LuaTypeBridge(标量转换)

`Source/Common/Script/Lua/LuaTypeBridge.h` — `PushInteger/PushBoolean/PushNumber/PushString/PushNil` 与 `PopInteger/PopBoolean/PopNumber/PopString`,均为 `TResult` 包裹、类型检查走 `IsInteger/IsBoolean/IsNumber/IsString/IsNil`。

### 其他模块

`LuaModule`(MLuaModule,模块注册到 `Mession[Name]`)、`LuaCoroutineBridge`(协程桥)、`LuaHotReload`(热重载)、`LuaRepl`(REPL)、`MobDebugServer`(调试协议)、`FPendingCall`(挂起的 Lua 调用)。

### 错误处理约定

- 错误码统一来自 `ScriptErrorCodes`(`kNotFound` / `kInvalidArg` / `kClassNotFound` / `kMethodNotFound` / `kFactoryReturnNil` / `kInstanceReleased` 等),以 `[code] message` 形式随 `TResult` 返回;脚本侧用 `pcall` 捕获,不 panic。
- Lua 调 C++ 返回值约定:成功返回 `(value, nil)`,失败返回 `(nil, errstr)`。

## C++ 对象创建(脚本实例句柄)

让 C++ 框架层按字符串类名创建 Lua class 实例并长期持有(跨 VM 通用,当前 Lua 5.4 已落地)。

- **句柄**:`TScriptInstanceHandle`(`Source/Common/Script/Abstract/TScriptInstanceHandle.h`),opaque `int Id`;Lua 实现中 Id 即 `luaL_ref(LUA_REGISTRYINDEX)` 的引用值,`InvalidId = -1`,`IsValid()` 判空。`SetId` 为跨进程序列化预留。
- **创建**:`TResult<TScriptInstanceHandle> CreateInstanceByClassName(ClassName, Args)` — 从全局取 Lua class;若含 `:new` 函数则走 `:new(class, args...)`,否则 fallback `__call(class, args...)`;结果为 `nil` 返回 `factory_returned_nil`;`luaL_ref` 存入 registry 并返回句柄。
- **调用方法**:`TResult<TVariant> InvokeInstanceMethod(Handle, MethodName, Args)` — 按句柄取对象(必须是 table/userdata),取方法、`lua_pcall` 调用,返回值转 `TVariant`(Null/Bool/Int/Double/String 五类);句柄已释放返回 `instance_released`,方法不存在返回 `method_not_found`。
- **释放**:`ReleaseInstance(Handle)` → `luaL_unref(L, LUA_REGISTRYINDEX, Id)`。
- **错误透传**:`:new` / 方法抛 Lua 错误时,错误消息原样透传回 `TResult`。
- 测试:`Source/Common/Script/Lua/Tests/TestLuaInstanceHandle.cpp`(8 例,覆盖 `:new` 路径、`__call` fallback、类不存在、`new` 抛错、方法调用/不存在、GC 后与显式释放后访问)。

## 标量值桥(MScalarValue)

`Source/Common/Script/Lua/MLuaVector.h` / `MLuaVector.cpp` — C++ 与 Lua 之间统一的标量容器,供 Vector / Map / 反射参数等多个桥共享。

```cpp
enum class MScalarType : uint8 { Int, Double, String };

struct MScalarValue {
    MScalarType Type;
    int64       IntVal;
    double      DoubleVal;
    MString     StringVal;
    // MakeInt / MakeDouble / MakeString 构造
    // static TResult<MScalarValue> FromLua(lua_State* L, int32 Index);
    // void PushToLua(lua_State* L) const;
    // MString ToDebugString() const;
};
```

- 类型推断在 `:set` 时按 Lua 栈类型决定;`:get` 时按 `Type` 转回 Lua number / string。
- 支持 `operator==/!=`(先比类型再比值),便于测试。

## 反射桥

业务侧在脚本里查询类、调用方法(热路径走 `MFunction::Invoke` 直接调 C++,不走 Lua 解释)。

- `Mession.FindClass(name)` → 返回 `MClassProxy` metatable(类不存在返回 `nil` + `not_found` 错误)。
- `Mession.InvokeClass(class, method, args...)` → 成功 `(ok_value, nil)` / 失败 `(nil, errstr)`;实现为 `MObject::FindClass` → `MClass::FindFunction` → `BuildScriptArgs` → `MFunction::NativeInvoke` / `ServerCallHandler` → `SFutureResult` 桥接。
- `Mession.GetGlobal(key)` / `Mession.SetGlobal(key, value)` → 走 `IScriptEngine::Get/SetGlobal`(引擎已实现)。
- `Mession.RegisterModule(name, proxy_table)` → 框架侧调用,把 MClass 反射 proxy 挂到 `Mession[name]`,不直接暴露给业务脚本。
- **MClassProxy 元表**:`__index` 先查 `MClass::FindProperty`(属性读),再查 `FindFunction`(返回闭包调 `InvokeClass`);`__newindex` 走 `SetProperty`;`__tostring` 返回类名。
- **数据流**:`Lua: Mession.InvokeClass(...)` → C function → 查 MClass → 查函数 → `BuildScriptArgs` → `CallFunction` → `SFutureResult` → `lua_resume` 推结果。
- 业务侧对象生命周期规则:只持 ActorId,不持对象引用。

## Vector 桥

把 C++ `TVector<T>` 桥到 Lua userdata。

- **创建**:`Mession.Vector.new([size [, init]])` — `size=0, init=nil` 走 value-initialize;`lua_newuserdata` + metatable `MVectorProxy`。
- **实例方法**:`:get(i)`(1-based,越界返回 `nil, out_of_range`)、`:set(i, v)`(越界报错)、`:push(v)`、`:pop()`(空返回 `nil, empty`)、`:insert(i, v)`(`i=size+1` 等价 push)、`:remove(i)`(返回被移除元素)、`:size()`、`:clear()`、`:clone()`(深拷贝)。
- **元方法**:`__index`(数值 key 走 `tv[i-1]`,非数值转发 method 表)、`__newindex`(数值 key 赋值,越界抛错)、`__len`、`__pairs`(ipairs 风格)、`__gc`(delete 释放 C++ `TVector`)、`__tostring`(`"Vector(size=N)"`)、`__eq`(长度 + 元素全等)。
- **标量规则**:Lua 栈类型 → C++ 元素 `int64 / double / MString`;bool 及其它类型拒绝(返回 `nil, type_error`)。
- 内存所有权:userdata 直接持有 `TVector*`,Lua GC 时 `delete`。

## Map 桥

把 C++ `TMap<K,V>`(`std::unordered_map`)桥到 Lua userdata。

- **创建**:`Mession.Map.new([size_hint])` — size_hint 作 `reserve` 桶数提示;`lua_newuserdata` + metatable `MMapProxy`。
- **Key**:必须 Lua `string`(非 string 显式报错);**Value**:与 Vector 相同的三种标量 `int64 / double / MString`。
- **实例方法**:`:get(k[, default])`(不存在 → `nil` 或 default)、`:set(k, v)`、`:delete(k)`(返回是否删到)、`:size()`、`:clear()`、`:contains(k)`、`:keys()` / `:values()`(返回 vector-like userdata,单次上限 65536,超限报 `too large`)、`:clone()`(深拷贝)。
- **元方法**:`__index`(等同 `:get`)、`__newindex`(等同 `:set`)、`__pairs`(迭代前快照 keys,迭代期间不改)、`__len`(等价 `:size`)、`__gc`(delete `TMap`)、`__tostring`(`"Map(size=N)"`)、`__eq`(长度 + key/value 全等)。
- 生命周期:userdata 直接持有 `TMap*`,`__gc` 时释放。

## 日志与格式化桥

业务侧用 `Mession.log.*` 与 `Mession.format`(桥接 `MLog` 与 `MFormat`,底层 `fmt::format`)。

- **`Mession.log.{category}.{level}(fmt, ...)`**:
  - category:`core`(LogCore)/ `net`(LogNet)/ `rpc`(LogRpc)/ `db`(LogDb)/ `auth`(LogAuth)/ `scene`(LogScene)共 6 类;
  - level:`debug` / `info` / `warn` / `error` / `fatal` 共 5 级 → **6 × 5 = 30 个入口**;
  - 实现为 C function 直调 `MLog::Write(Category, Level, fmt, ...)`(不抛异常);格式错误时仍写一条 `log format error` 错误行;日志写盘异步(ring + dispatcher + sinks),不阻塞 Lua。
- **`Mession.format(fmt, ...)`**:返回格式化字符串,格式串为 `fmt::format` 语法(`{}`、`{:06d}` 等);`Format` 抛异常 → `lua_error` → `pcall` 捕获。
  - 注意:`MFormat::Format` 当前只接 `(fmt)` 单参数,**不支持 var args**;多参数拼接需业务侧用 `string.format` 或多次 format。

## RPC / ID / Time 桥

- **`Mession.rpc.call(class, method, args...)`**:同步 RPC,返回 `(value, nil)` 成功 / `(nil, errstr)` 失败;实现为 `MRpcChannel::Call` 走 `SFutureResult<T>`,阻塞等待可走 `lua_yield`。只覆盖已在 `MClientManifest` 注册的函数。
- **`Mession.rpc.call_async(class, method, args..., callback)`**:异步 RPC,`SFutureResult` 完成时回调 `function(resp, err)`,不阻塞 Lua;callback 用 weak ref 跟踪 registry,需业务侧管理生命周期防泄漏。
- **`Mession.uniqueId()`**:返回 `MUniqueIdGenerator::Generate()` 的 `uint64` 作 Lua number;`id > 2^53` 时精度丢失并打 `LOG_WARN`,建议 ActorId 控制在 2^53 内或改用字符串 id。
- **`Mession.time.now()`**:`std::chrono::steady_clock` 毫秒(int64 → Lua number,同样受精度限制)。
- **`Mession.time.sleep(ms)`**:`std::this_thread::sleep_for`;**会阻塞 Lua 协程,不推荐在游戏循环使用**(推荐 Lua 协程 + C++ Timer)。

## 相关实现

| 模块 | 文件 |
|---|---|
| 抽象层接口 | `Source/Common/Script/Abstract/IScriptEngine.h`、`TScriptInstanceHandle.h`、`TVariant.h`、`ScriptErrorCodes.h`、`SScriptEngineConfig.h` |
| Lua 引擎 | `Source/Common/Script/Lua/LuaEngine.h/.cpp`(MLuaEngine)、`LuaScriptState.h/.cpp`(MLuaScriptState) |
| 类型/协程/模块 | `Source/Common/Script/Lua/LuaTypeBridge.h/.cpp`、`LuaCoroutineBridge.h/.cpp`、`LuaModule.h/.cpp`、`FPendingCall.h` |
| 工具 | `Source/Common/Script/Lua/LuaHotReload.h/.cpp`、`LuaRepl.h/.cpp`、`MobDebugServer.h/.cpp` |
| 标量桥(Vector/Map 共用) | `Source/Common/Script/Lua/MLuaVector.h/.cpp`(MScalarType / MScalarValue) |
| 实例句柄测试 | `Source/Common/Script/Lua/Tests/TestLuaInstanceHandle.cpp`(以及 `TestLuaTypeBridge.cpp`、`TestLuaCoroutine.cpp`) |
| 构建 | `Source/Common/Script/Lua/CMakeLists.txt`、`Source/Common/Script/Lua/Tests/CMakeLists.txt` |
| 底层依赖 | `Source/Common/Runtime/Log/Log.h`(MLog)、`Source/Common/Runtime/StringUtils.h`(MFormat)、`Source/Common/Net/Rpc/MRpcChannel.h`、`Source/Common/Runtime/Id.h`(MUniqueIdGenerator)、`Source/Common/Runtime/Time.h`(MTime) |

> 各桥的规范来源:`Docs/superpowers/specs/2026-08-07-lua-reflect-bridge.md`(反射)、`...-lua-vector-bridge.md`(Vector)、`...-lua-map-bridge.md`(Map)、`...-lua-log-bridge.md`(日志/格式化)、`...-lua-rpc-bridge.md`(RPC/ID/Time)、`2026-08-08-lua-cpp-create-instance.md`(脚本实例句柄)。
