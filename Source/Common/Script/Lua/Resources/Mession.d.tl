--
-- Mession Lua stdlib 桥提示文件 (Teal 静态类型检查风格)
--
-- 此文件仅供 Teal type checker 用,**运行时不会被 require**。
-- 实际 `Mession.*` 全局由 C++ 端在 MLuaEngine 启动时通过
--   MLuaVector::Install(L) / MLuaMap::Install(L) / MLuaLog::Install(L)
--   MLuaFormat::Install(L) / MLuaRpc::Install(L, &engine)
-- 注册。
--
-- 修改本文件时:同步更新 Mession.lua(lua-language-server 风格),保持签名一致。
--
-- MScalarValue 桥接约定:
--   C++ 端 MLuaVector/Map::MScalarValue = int64 | double | MString
--   Lua → C++ 方向:无小数点的 number 走 int64,带小数点走 double
--   C++ → Lua 方向:int64 走 lua_Integer,double 走 lua_Number
--   boolean / nil 通过包装传递;table / userdata / function 走 "<nil>" 占位

local record MScalar
    type Type = integer | number | string | boolean
end

-- ============================================================================
-- Vector
-- ============================================================================

local record MVector
    userdata

    new: function(size?: integer, init?: MScalar.Type): MVector
    size: function(self): integer
    push: function(self, v: MScalar.Type): integer
    pop:  function(self): MScalar.Type
    get:  function(self, i: integer): MScalar.Type
    set:  function(self, i: integer, v: MScalar.Type)
    insert: function(self, i: integer, v: MScalar.Type)
    remove: function(self, i: integer): MScalar.Type
    clear:  function(self)
    clone:  function(self): MVector

    -- metamethods: __len / __pairs / __index / __newindex / __gc
end

-- ============================================================================
-- Map
-- ============================================================================

local record MMap
    userdata

    new: function(): MMap
    get:    function(self, key: MScalar.Type): MScalar.Type
    set:    function(self, key: MScalar.Type, val: MScalar.Type)
    has:    function(self, key: MScalar.Type): boolean
    remove: function(self, key: MScalar.Type): MScalar.Type
    size:   function(self): integer
    clear:  function(self)
    keys:   function(self): {MScalar.Type}
    values: function(self): {MScalar.Type}

    -- metamethods: __len / __pairs / __index / __newindex / __gc
end

-- ============================================================================
-- Log
-- ============================================================================

local record MLog
    userdata
end

local function _log_call(msg: string) end

local record MLogMeta
    type logfn = function(msg: string)

    userdata
    info:  logfn
    warn:  logfn
    error: logfn
    debug: logfn
    fatal: logfn
end

-- ============================================================================
-- Format
-- ============================================================================

local record MFormat
    userdata

    fmt:      function(tpl: string, ...: any): string
    concat:   function(...: any): string
    split:    function(s: string, sep: string): {string}   -- sep 必须是单字符
    trim:     function(s: string): string
    tostring: function(v: any): string
end

-- ============================================================================
-- RPC
-- ============================================================================

local record MRPC
    userdata

    -- 错误时返回 (nil, "not_a_function") 或 (nil, "pcall error msg")
    call: function(name: string, ...: any): any
end

-- ============================================================================
-- Time
-- ============================================================================

local record MTime
    userdata

    now:     function(): number     -- 浮点秒
    nowMs:   function(): integer    -- 毫秒
    sleepMs: function(ms: integer)
end

-- ============================================================================
-- Id
-- ============================================================================

local record MId
    userdata

    new: function(): integer
end

-- ============================================================================
-- Mession 顶层
-- ============================================================================

local record Mession
    userdata

    Vector: MVector
    Map:    MMap
    Log:    MLogMeta
    Format: MFormat
    RPC:    MRPC
    Time:   MTime
    Id:     MId
end

return Mession