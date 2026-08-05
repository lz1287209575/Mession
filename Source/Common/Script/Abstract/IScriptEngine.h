#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Script/Abstract/EScriptLanguage.h"
#include "Common/Script/Abstract/EReloadMode.h"
#include "Common/Script/Abstract/EReloadResult.h"
#include "Common/Script/Abstract/SScriptEngineConfig.h"
#include "Common/Script/Abstract/TVariant.h"
#include "Common/Script/Abstract/IScriptModule.h"

namespace mession::script {

// SScriptArgs 占位 — 后续 Script 桥接 plan 充实具体实现
// 当前仅含 TVariant 数组 + 长度
struct SScriptArgs
{
    const TVariant* Values = nullptr;
    size_t          Count  = 0;

    SScriptArgs() = default;
    SScriptArgs(const TVariant* InValues, size_t InCount)
        : Values(InValues), Count(InCount) {}
};

using TScriptArgs = SScriptArgs;

// IScriptEngine 顶层接口 — 12 方法
// 不暴露 Pin/Unpin/Borrow/GetPlayer(权威基线见 2026-08-04-object-lifecycle.md)
class IScriptEngine
{
public:
    virtual ~IScriptEngine() = default;

    // ====== 生命周期 ======
    virtual TResult<void>     Initialize(const SScriptEngineConfig& Cfg) = 0;
    virtual void              Tick(float DeltaSeconds)                  = 0;
    virtual void              StepCoroutines()                          = 0;
    virtual TResult<void>     Shutdown()                                = 0;

    // ====== 热重载统一入口 ======
    virtual TResult<EReloadResult> Reload(EReloadMode Mode) = 0;

    // ====== Script → C++ 反射调用 ======
    virtual TResult<void> CallFunction(MFunctionObject* Fn, const TScriptArgs& Args)          = 0;
    virtual TResult<void> CallFunctionById(uint64 FunctionId, const TScriptArgs& Args)      = 0;

    // ====== 模块管理 ======
    virtual TSharedPtr<IScriptModule> CreateModule(MClass* OwningClass)                = 0;
    virtual void                      RegisterModule(TSharedPtr<IScriptModule> Module) = 0;
    virtual void                      UnregisterModule(MClass* OwningClass)            = 0;

    // ====== 全局读写 ======
    virtual void                  SetGlobal(TStringView Key, const TVariant& Value) = 0;
    virtual TResult<TVariant>     GetGlobal(TStringView Key)                       = 0;

    // ====== 错误传递 ======
    virtual MString GetErrorString(int32 State, void* Frame) const = 0;

    // ====== 元信息 ======
    virtual EScriptLanguage GetLanguage() const = 0;
    virtual bool            IsSandboxed() const = 0;
};

} // namespace mession::script