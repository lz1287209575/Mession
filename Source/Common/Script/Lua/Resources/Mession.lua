--- @meta
---
-- Mession Lua stdlib 桥提示文件 (lua-language-server 风格)
--
-- 此文件仅供 IDE 补全 / 类型检查用,**运行时不会被 require**。
-- 实际 `Mession.*` 全局由 C++ 端在 MLuaEngine 启动时通过
--   MLuaVector::Install(L)
--   MLuaMap::Install(L)
--   MLuaLog::Install(L)
--   MLuaFormat::Install(L)
--   MLuaRpc::Install(L, &engine)
-- 注册;运行时调用走 C++ cfunction,不读本文件。
--
-- 修改本文件时:同步更新 Mession.d.tl(Teal 静态检查版本),保持签名一致。
--
-- 返回类型约定:
--   MScalarValue = integer | number | string | boolean
--   Lua 端调用 push/set/get 时,整数(无小数点)按 int64,带小数点按 double,
--   其余类型走 string 路径(参考 MScalarValue::FromLua 实现)。

-- ============================================================================
-- Vector
-- ============================================================================

--- @class MVector
local MVector = {}

--- 创建一个空 Vector
--- @return MVector
function MVector.new() end

--- 创建指定长度 Vector,所有元素初始化为 init
--- @param size integer
--- @param init? integer|number|string|boolean
--- @return MVector
function MVector.new(size, init) end

--- @return integer
function MVector:size() end

--- 追加元素(返回修改后 size)
--- @param v integer|number|string|boolean
--- @return integer
function MVector:push(v) end

--- 弹出末尾元素;空表返回 nil
--- @return integer|number|string|boolean|nil
function MVector:pop() end

--- 取 1-based 索引处元素,越界抛错
--- @param i integer
--- @return integer|number|string|boolean
function MVector:get(i) end

--- 设置 1-based 索引处元素,越界抛错
--- @param i integer
--- @param v integer|number|string|boolean
--- @return nil
function MVector:set(i, v) end

--- 在 1-based 索引处插入元素
--- @param i integer
--- @param v integer|number|string|boolean
--- @return nil
function MVector:insert(i, v) end

--- 删除 1-based 索引处元素并返回该元素
--- @param i integer
--- @return integer|number|string|boolean
function MVector:remove(i) end

--- 清空所有元素
--- @return nil
function MVector:clear() end

--- 返回浅拷贝(内部 vector 重新分配)
--- @return MVector
function MVector:clone() end

-- ============================================================================
-- Map
-- ============================================================================

--- @class MMap
local MMap = {}

--- @return MMap
function MMap.new() end

--- 取键对应值;键不存在返回 nil
--- @param key integer|number|string|boolean
--- @return integer|number|string|boolean|nil
function MMap:get(key) end

--- @param key integer|number|string|boolean
--- @param val integer|number|string|boolean
--- @return nil
function MMap:set(key, val) end

--- @param key integer|number|string|boolean
--- @return boolean
function MMap:has(key) end

--- 删除并返回值;键不存在返回 nil
--- @param key integer|number|string|boolean
--- @return integer|number|string|boolean|nil
function MMap:remove(key) end

--- @return integer
function MMap:size() end

--- @return nil
function MMap:clear() end

--- 返回所有键(普通 Lua 数组表,非 MVector)
--- @return integer[]|number[]|string[]|boolean[]
function MMap:keys() end

--- 返回所有值(普通 Lua 数组表,非 MVector)
--- @return integer[]|number[]|string[]|boolean[]
function MMap:values() end

-- ============================================================================
-- Log
-- ============================================================================

--- @class MLog
local MLog = {}

--- @param msg string
--- @return nil
function MLog.info(msg) end
function MLog.warn(msg) end
function MLog.error(msg) end
function MLog.debug(msg) end
function MLog.fatal(msg) end

-- ============================================================================
-- Format
-- ============================================================================

--- @class MFormat
local MFormat = {}

--- fmt-style 格式化(支持 {} / {:fmt} / {0} 等)
--- @param tpl string
--- @param ... any
--- @return string
function MFormat.fmt(tpl, ...) end

--- 连接多个值成单字符串(非 string 走 Lua tostring)
--- @param ... any
--- @return string
function MFormat.concat(...) end

--- 按单字符 sep 拆字符串
--- @param s string
--- @param sep string  # 必须是单字符
--- @return string[]
function MFormat.split(s, sep) end

--- 去首尾空白
--- @param s string
--- @return string
function MFormat.trim(s) end

--- 等价于 Lua 内置 tostring
--- @param v any
--- @return string
function MFormat.tostring(v) end

-- ============================================================================
-- RPC
-- ============================================================================

--- @class MRPC
local MRPC = {}

--- 按 Lua 全局函数名调用;name 不存在返回 (nil, "not_a_function")
--- @param name string
--- @param ... any
--- @return any
function MRPC.call(name, ...) end

-- ============================================================================
-- Time
-- ============================================================================

--- @class MTime
local MTime = {}

--- @return number  # 浮点秒(steady_clock)
function MTime.now() end

--- @return integer  # 毫秒(steady_clock * 1000)
function MTime.nowMs() end

--- @param ms integer
--- @return nil
function MTime.sleepMs(ms) end

-- ============================================================================
-- Id
-- ============================================================================

--- @class MId
local MId = {}

--- @return integer  # 自增唯一 ID
function MId.new() end

-- ============================================================================
-- Mession 顶层
-- ============================================================================

--- @class Mession
--- @field Vector { new: fun(size?: integer, init?: integer|number|string|boolean): MVector }
--- @field Map { new: fun(): MMap }
--- @field Log MLog
--- @field Format MFormat
--- @field RPC MRPC
--- @field Time MTime
--- @field Id MId

--- @type Mession
local Mession = {}
return Mession