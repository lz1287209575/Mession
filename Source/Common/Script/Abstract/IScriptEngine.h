/**
 * @file IScriptEngine.h
 * @brief IScriptEngine —— 脚本引擎顶层接口(24 个纯虚方法)与 SScriptArgs 参数包。
 *
 * @detail 职责边界:不含对象生命周期管理(不暴露 Pin/Unpin/Borrow/GetPlayer)。
 *   全部方法均可能被调用于同一线程内的任意时刻,线程安全由具体实现自述;
 *   MLuaEngine 用 MSharedMutex 保护状态切换,读路径由调用方持 shared lock。
 *   当前唯一实现是 Lua 侧的 MLuaEngine;Python/TS/C# 后端尚未落地。
 *
 * 典型时序(以 MLuaEngine 为例):
 *   Initialize() → (Tick / StepCoroutines 每帧) → Reload(Mode) → Shutdown()
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Script/Abstract/EReloadMode.h"
#include "Common/Script/Abstract/EReloadResult.h"
#include "Common/Script/Abstract/EScriptLanguage.h"
#include "Common/Script/Abstract/IScriptModule.h"
#include "Common/Script/Abstract/SScriptEngineConfig.h"
#include "Common/Script/Abstract/TScriptInstanceHandle.h"
#include "Common/Script/Abstract/TVariant.h"

namespace mession::script {

    /**
     * @brief SScriptArgs - 一次脚本/C++ 互调的实参包(非拥有视图)。
     *
     * @detail 只借用 TVariant 数组,不拷贝、不接管所有权:Values 必须比调用活得久,
     *   Count 为元素个数。当前仅承载标量 TVariant,复杂类型待桥接层补齐。
     */
    struct SScriptArgs {
        const TVariant* Values = nullptr; // 借用;生命周期由调用方保证
        size_t          Count  = 0;       // Values 中有效元素个数

        SScriptArgs() = default;
        SScriptArgs(const TVariant* InValues, size_t InCount) : Values(InValues), Count(InCount) {
        }
    };

    using TScriptArgs = SScriptArgs;

    /**
     * @brief IScriptEngine - 脚本引擎顶层接口。
     *
     * @detail 不暴露 Pin/Unpin/Borrow/GetPlayer 等对象生命周期操作 —— 那些由
     *   Actor / ObjectDomain 体系负责,不在脚本引擎层。
     */
    class IScriptEngine {
        public:
        virtual ~IScriptEngine() = default;

        // ====== 生命周期 ======
        /** @brief Initialize - 创建 VM、装配标准库;失败返回 Err 且引擎不可用 */
        virtual TResult<void> Initialize(const SScriptEngineConfig& Cfg) = 0;
        /** @brief Tick - 每帧推进(DeltaSeconds 单位:秒);当前 Lua 实现为空实现 */
        virtual void Tick(float DeltaSeconds) = 0;
        /** @brief StepCoroutines - 推进所有挂起协程(通常每帧调一次) */
        virtual void StepCoroutines() = 0;
        /** @brief Shutdown - 释放 VM 与模块;可重复调用 */
        virtual TResult<void> Shutdown() = 0;

        // ====== 热重载统一入口 ======
        /** @brief Reload - 按 Mode 重载脚本;数据源由实现自行约定 */
        virtual TResult<EReloadResult> Reload(EReloadMode Mode) = 0;

        // ====== Script → C++ 反射调用 ======
        /** @brief CallFunction - 按反射函数对象调用(C++ 侧入口) */
        virtual TResult<void> CallFunction(MFunctionObject* Fn, const TScriptArgs& Args) = 0;
        /** @brief CallFunctionById - 按全局函数 id 查找后调用 */
        virtual TResult<void> CallFunctionById(uint64 FunctionId, const TScriptArgs& Args) = 0;

        // ====== Script → C++ 创建业务对象 ======
        /** @brief CreateInstance - 反射创建 MObject 实例;所有权移交调用方 */
        virtual TResult<MObject*> CreateInstance(MClass* Cls, const TScriptArgs& Args) = 0;
        /** @brief CreateActor - 创建脚本侧 actor,返回 ActorId */
        virtual TResult<uint64> CreateActor(MClass* Cls, const TScriptArgs& Args) = 0;

        // ====== C++ 持有脚本侧 class 实例(跨 VM 通用) ======
        /**
         * @brief CreateInstanceByClassName - 按类名字符串在脚本侧创建实例。
         * @param ClassName 脚本全局表中的类名
         * @param Args      透传给工厂(new / __call)的参数
         * @return 实例句柄;底层对象被 GC 回收后该句柄失效
         *
         * @detail VM 内部用 registry ref / PyObject* / JSValue / GCHandle 映射到自己的对象。
         */
        virtual TResult<TScriptInstanceHandle> CreateInstanceByClassName(const MString& ClassName, const TScriptArgs& Args) = 0;

        /**
         * @brief InvokeInstanceMethod - 在句柄对应实例上调用方法。
         * @param Handle     由 CreateInstanceByClassName 获得的句柄
         * @param MethodName 方法名
         * @param Args      实参
         * @return 返回值统一装进 TVariant;无法映射的返回类型退化为 Null
         */
        virtual TResult<TVariant> InvokeInstanceMethod(TScriptInstanceHandle Handle, const MString& MethodName, const TScriptArgs& Args) = 0;

        /** @brief ReleaseInstance - 释放句柄占用的脚本侧引用(幂等) */
        virtual void ReleaseInstance(TScriptInstanceHandle Handle) = 0;

        // ====== 模块管理 ======
        /** @brief CreateModule - 为 OwningClass 创建并登记脚本模块 */
        virtual TSharedPtr<IScriptModule> CreateModule(MClass* OwningClass) = 0;
        /** @brief RegisterModule - 登记一个已构造的模块(当前 Lua 实现为 no-op) */
        virtual void RegisterModule(TSharedPtr<IScriptModule> Module) = 0;
        /** @brief UnregisterModule - 注销 OwningClass 的模块(当前 Lua 实现为 no-op) */
        virtual void UnregisterModule(MClass* OwningClass) = 0;

        // ====== 全局读写 ======
        /** @brief SetGlobal - 写脚本全局变量;未初始化时静默忽略 */
        virtual void SetGlobal(MStringView Key, const TVariant& Value) = 0;
        /** @brief GetGlobal - 读脚本全局变量;不存在的 key 返回 Null */
        virtual TResult<TVariant> GetGlobal(MStringView Key) = 0;

        // ====== 错误传递 ======
        /** @brief GetErrorString - 把 VM 错误状态/栈帧抽成可读字符串 */
        virtual MString GetErrorString(int32 State, void* Frame) const = 0;

        // ====== 元信息 ======
        /** @brief GetLanguage - 本引擎实现的脚本语言 */
        virtual EScriptLanguage GetLanguage() const = 0;
        /** @brief IsSandboxed - 是否运行在沙箱(限制 IO / 危险 API) */
        virtual bool IsSandboxed() const = 0;

        // ====== DualVM 热重载专用 ======

        /**
         * @brief SaveAllActorStates - 快照所有脚本侧 actor 的持久化状态。
         * @return key = ActorId(uint64);value = opaque state blob(格式由 VM 自定)
         *
         * @detail Lua 走 __dualvm_save 钩子 → string;C++ 反射 fallback 可由实现者扩展。
         */
        virtual TMap<uint64, MString> SaveAllActorStates() = 0;

        /**
         * @brief RestoreAllActorStates - 在新 VM 上重建 actor 并恢复状态。
         * @param Snapshot SaveAllActorStates 的产物
         *
         * @detail 由 MLuaHotReload::ReloadDualVM 在 BeginSwap 之后调用。
         */
        virtual void RestoreAllActorStates(const TMap<uint64, MString>& Snapshot) = 0;

        /**
         * @brief RebindCrossInstanceRefs - 遍历所有 actor 并回调 OnVmSwapped(OldH, NewH)。
         */
        virtual void RebindCrossInstanceRefs() = 0;

        /**
         * @brief GetActorHandle - 按 ActorId 查当前 generation 的句柄。
         * @param ActorId 目标 actor
         * @return 新句柄;不存在时返回 Err(kActorNotFound)
         */
        virtual TResult<TScriptInstanceHandle> GetActorHandle(uint64 ActorId) = 0;
    };

} // namespace mession::script