#pragma once

#include "Common/Runtime/MLib.h"

#include <mutex>

extern "C" {
struct lua_State;
}

namespace mession::script::lua {

    // FLuaPendingCall — 表示一个 pending 的 lua 调用(协程 yield 后挂起)
    // 每个 instance:
    //   - 持有一个 lua_newthread 出来的 coroutine lua_State* (CoThread)
    //   - 该 CoThread 必须锚在主 VM 的 registry 里(否则被 GC 回收)
    //   - bPending = true 表示还在挂起
    //
    // 实际场景:业务侧代码做异步 IO 时,cps 变换为 lua_resume 启动协程;
    // 协程 yield,挂起;C++ IO 完成时,调 Resume(返回值) 唤醒协程。
    class FLuaPendingCall {
        public:
        FLuaPendingCall() = default;
        ~FLuaPendingCall();

        FLuaPendingCall(const FLuaPendingCall&)            = delete;
        FLuaPendingCall& operator=(const FLuaPendingCall&) = delete;

        // InitYield:把 CoThread 锚到主 VM 的 registry(防 GC),bPending=true
        // 必须从主 VM 线程调用(不要在协程 yield 后调)
        void InitYield(lua_State* InMainL, lua_State* InCoThread, int32 InNArgs);

        // Resume:用 OutVals/OutErrs 当 resume args 唤醒协程,设 bPending=false
        // 后续 tick 时通过 LuaCoroutineBridge::ResumeFromStack 真正 resume
        void EnqueueResume(const MString* OutVals, size_t OutCount,
                          const MString* OutErrs, size_t ErrCount);

        // 实际 resume:在 tick 时调(主 VM 线程)
        // 返回 ECoroutineResumeStatus
        int ResumeNow();

        bool IsPending() const {
            return bPending;
        }
        bool HasResumeArgs() const {
            return bResumePending;
        }
        void Cancel() {
            bPending      = false;
            bResumePending = false;
        }

        lua_State* GetCoThread() const {
            return CoThread;
        }

        private:
        lua_State* MainL      = nullptr; // 主 VM 的 lua_State*(用来 lua_pushvalue + luaL_ref)
        lua_State* CoThread   = nullptr; // 协程 lua_State*(lua_newthread 结果,锚在 MainL 上)
        int32      ThreadRef  = -1;     // MainL 上 CoThread 锚的 ref
        int32      NArgs      = 0;

        bool bPending      = false;
        bool bResumePending = false;    // EnqueueResume 后待 tick 时真正 resume

        // 待 push 到协程栈上的 resume 参数
        TVector<MString> PendingVals;
        TVector<MString> PendingErrs;
    };

    // FLuaPendingCallRegistry — per-engine 的 pending call 注册表
    // 负责:
    //   - 分配 LocalId
    //   - StepAll 每帧把所有 pending + 有 resume args 的 call 真正 resume
    //   - CountPending 给 DrainPendingCalls 用
    class FLuaPendingCallRegistry {
        public:
        FLuaPendingCallRegistry() = default;
        ~FLuaPendingCallRegistry() = default;

        // 注册一个新 pending call;返回 local id(后续可通过 Get 找回)
        uint32 Register(TSharedPtr<FLuaPendingCall> Call);

        // 注销(协程完成或 cancel 时)
        void Unregister(uint32 LocalId);

        // 查
        TSharedPtr<FLuaPendingCall> Get(uint32 LocalId) const;

        // 当前 pending(call.IsPending == true)的数量,供 Drain 用
        uint32 CountPending() const;

        // 当前总数(含已完成但未 Unregister 的)
        uint32 CountTotal() const;

        // 每帧调:把所有 bResumePending 的 call 真正 resume
        // 返回本次 resume 的 call 数(可观察)
        uint32 StepAll();

        private:
        mutable std::mutex                                       Mutex;
        TMap<uint32, TSharedPtr<FLuaPendingCall>>                 Calls;
        std::atomic<uint32>                                       NextLocalId{1};
    };

} // namespace mession::script::lua