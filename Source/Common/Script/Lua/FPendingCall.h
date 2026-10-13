/**
 * @file FPendingCall.h
 * @brief FLuaPendingCall / FLuaPendingCallRegistry —— 挂起协程的登记与唤醒。
 *
 * @detail 生命周期:业务侧把异步 IO 的 cps 变换成 lua_resume 启动协程 → 协程 yield
 *   后由 InitYield 锚住(防 GC)→ IO 完成时 EnqueueResume → 每帧 StepAll 里
 *   ResumeNow 真正唤醒。Registry 自带 std::mutex,是唯一可以跨线程使用的部分。
 */
#pragma once

#include "Common/Runtime/MLib.h"

#include <mutex>

extern "C" {
struct lua_State;
}

namespace mession::script::lua {

    /**
     * @brief FLuaPendingCall - 一个挂起中的协程调用。
     *
     * @detail 每个实例持有一个由 lua_newthread 产生的协程 lua_State*(CoThread),
     *   并把它锚在主 VM 的 registry 里(否则会被 GC 回收)。bPending 表示是否仍挂起。
     *
     * 调用线程约定:InitYield / EnqueueResume / ResumeNow 都必须在主 VM 所在线程调用,
     *   Registry 的分发由 FLuaPendingCallRegistry 统一保证。
     */
    class FLuaPendingCall {
        public:
        FLuaPendingCall() = default;
        ~FLuaPendingCall();

        FLuaPendingCall(const FLuaPendingCall&)            = delete;
        FLuaPendingCall& operator=(const FLuaPendingCall&) = delete;

        /**
         * @brief InitYield - 记录协程并把 CoThread 锚进主 VM 的 registry。
         * @param InMainL    主 VM 的 lua_State*
         * @param InCoThread 协程的 lua_State*(须已 push 在主 VM 栈顶)
         * @param InNArgs    本次 resume 用的实参个数
         *
         * @detail 置 bPending=true。前置条件:调用方已把 CoThread 压在栈顶
         *   (lua_newthread / lua_pushthread),函数内部会对栈顶做 lua_pushvalue + luaL_ref。
         */
        void InitYield(lua_State* InMainL, lua_State* InCoThread, int32 InNArgs);

        /**
         * @brief EnqueueResume - 登记唤醒参数(仅入队,不在本调用内 resume)。
         * @param OutVals  值参数数组;可为 nullptr
         * @param OutCount OutVals 个数
         * @param OutErrs  错误参数数组;可为 nullptr
         * @param ErrCount OutErrs 个数
         *
         * @detail 参数会被拷贝保存。已 bPending=false(完成或已 Cancel)时直接丢弃。
         *   真正 resume 发生在后续 tick 的 ResumeNow。
         */
        void EnqueueResume(const MString* OutVals, size_t OutCount, const MString* OutErrs, size_t ErrCount);

        /**
         * @brief ResumeNow - 真正 resume 协程(在 tick 时、主 VM 线程调用)。
         * @return LUA_OK 协程正常结束 / LUA_YIELD 再次挂起 / 负值表示前置条件不满足(未 InitYield)
         *
         * @detail 先把 PendingErrs 再 PendingVals push 到协程栈,作为 resume 实参;
         *   结束后清空队列。协程报错时错误值会被 pop 丢弃且 bPending 置 false。
         */
        int ResumeNow();

        /** @brief IsPending - 是否仍处于挂起状态 */
        bool IsPending() const {
            return bPending;
        }
        /** @brief HasResumeArgs - 是否已有待处理的唤醒参数(待下次 tick 真正 resume) */
        bool HasResumeArgs() const {
            return bResumePending;
        }
        /**
         * @brief Cancel - 直接放弃该挂起调用(不 resume、不释放 registry ref)。
         *
         * @detail CoThread 的 registry ref 仍由析构函数 unref。
         */
        void Cancel() {
            bPending       = false;
            bResumePending = false;
        }

        lua_State* GetCoThread() const {
            return CoThread;
        }

        private:
        lua_State* MainL     = nullptr; // 主 VM 的 lua_State*(lua_pushvalue + luaL_ref 用)
        lua_State* CoThread  = nullptr; // 协程 lua_State*(lua_newthread 结果,锚在 MainL 上)
        int32      ThreadRef = -1;      // CoThread 在 MainL registry 中的锚 ref;析构 unref
        int32      NArgs     = 0;

        bool bPending       = false; // InitYield 置位,协程结束/出错/Cancel 清位
        bool bResumePending = false; // EnqueueResume 后置位,待 tick 时真正 resume

        // 待 push 到协程栈上的 resume 参数
        TVector<MString> PendingVals;
        TVector<MString> PendingErrs;
    };

    /**
     * @brief FLuaPendingCallRegistry - per-engine 的挂起调用注册表。
     *
     * @detail 负责分配 LocalId、每帧 StepAll 唤醒就绪的调用、给 DrainPendingCalls 提供计数。
     *   本类是线程安全的(std::mutex 保护表,NextLocalId 为 std::atomic)。
     *   当前没有自动回收:已完成的 call 需要调用方显式 Unregister。
     */
    class FLuaPendingCallRegistry {
        public:
        FLuaPendingCallRegistry()  = default;
        ~FLuaPendingCallRegistry() = default;

        /**
         * @brief Register - 登记一个待唤醒的调用。
         * @param Call 调用对象(共享所有权)
         * @return 新分配的 LocalId;Call 为空时返回 0
         *
         * @detail LocalId 从 1 递增,只增不减。
         */
        uint32 Register(TSharedPtr<FLuaPendingCall> Call);

        /** @brief Unregister - 按 LocalId 摘除(协程完成或 Cancel 后调用) */
        void Unregister(uint32 LocalId);

        /** @brief Get - 按 LocalId 查;不存在返回 nullptr */
        TSharedPtr<FLuaPendingCall> Get(uint32 LocalId) const;

        /** @brief CountPending - IsPending() 为真的数量(Drain 的退出条件) */
        uint32 CountPending() const;

        /** @brief CountTotal - 表中条目总数(含已完成但未 Unregister 的) */
        uint32 CountTotal() const;

        /**
         * @brief StepAll - 每帧唤醒所有已有 resume 参数的调用。
         * @return 本次实际调用 ResumeNow 的个数(不管协程是否再次 yield)
         *
         * @detail 先在锁内快照表内容,出锁后再逐个 ResumeNow,避免持锁执行 Lua。
         */
        uint32 StepAll();

        private:
        mutable std::mutex                        Mutex;
        TMap<uint32, TSharedPtr<FLuaPendingCall>> Calls;
        std::atomic<uint32>                       NextLocalId{1};
    };

} // namespace mession::script::lua