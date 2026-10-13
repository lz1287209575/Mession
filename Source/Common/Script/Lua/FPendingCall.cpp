/**
 * @file FPendingCall.cpp
 * @brief 实现挂起协程的锚定、唤醒与注册表并发分发。
 */
#include "Common/Script/Lua/FPendingCall.h"
#include "Common/Script/Lua/LuaCoroutineBridge.h"

#include <cassert>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    /**
     * @brief 析构 - 释放 CoThread 在 registry 上的锚(允许 GC 回收)。
     *
     * @detail 要求主 VM 此刻仍然存活(MainL 有效),否则 unref 是 UB。
     */
    FLuaPendingCall::~FLuaPendingCall() {
        if (MainL && ThreadRef != -1) {
            luaL_unref(MainL, LUA_REGISTRYINDEX, ThreadRef);
        }
    }

    /**
     * @brief InitYield - 锚定协程到主 VM registry。
     *
     * @detail 前置条件(MainL / InCoThread 非空)由 assert 检查;调用方须已把
     *   CoThread 压在 MainL 栈顶,本函数对栈顶做 pushvalue + luaL_ref。
     */
    void FLuaPendingCall::InitYield(lua_State* InMainL, lua_State* InCoThread, int32 InNArgs) {
        assert(InMainL && InCoThread);
        MainL    = InMainL;
        CoThread = InCoThread;
        NArgs    = InNArgs;
        bPending = true;

        // 把栈顶(已被调用方压入的 CoThread)锚进主 VM registry,防 GC
        lua_pushvalue(MainL, -1);
        ThreadRef = luaL_ref(MainL, LUA_REGISTRYINDEX);
    }

    /**
     * @brief EnqueueResume - 拷贝唤醒参数入队,不立即 resume。
     *
     * @detail 已非 pending(完成或 Cancel)时丢弃本次唤醒。参数被拷贝保存,
     *   因此调用方的原数组可以随即释放。
     */
    void FLuaPendingCall::EnqueueResume(const MString* OutVals, size_t OutCount, const MString* OutErrs, size_t ErrCount) {
        if (!bPending) {
            return; // 已完成或被 cancel:丢弃本次 resume
        }
        // 拷贝参数,等 tick 时再用
        PendingVals.assign(OutVals ? OutVals : nullptr, OutVals ? OutVals + OutCount : nullptr);
        PendingErrs.assign(OutErrs ? OutErrs : nullptr, OutErrs ? OutErrs + ErrCount : nullptr);
        bResumePending = true;
    }

    /**
     * @brief ResumeNow - 真正 resume 协程。
     * @return lua_resume 的返回码;前置条件不满足(非 pending / 无协程)时返回 -1
     *
     * @detail 先 push Errors 再 push Vals 到协程栈,二者合并成 resume 实参
     *   (调用方按 pcall 风格理解:错误在前、值在后)。无论结果如何都会清空待发队列。
     *   LUA_OK → 协程结束(bPending=false);LUA_YIELD → 仍挂起;
     *   其它 → 视为出错,bPending=false 并 pop 掉错误值(丢弃)。
     */
    int FLuaPendingCall::ResumeNow() {
        if (!bPending || !CoThread || !MainL) {
            return -1; // 前置条件不满足
        }

        // 先把 Errs 再 Vals push 到协程栈,作为 resume 实参
        // (错误在前、值在后;见头文件对 ResumeNow 的说明)
        int NArgsToPush = 0;
        for (size_t i = 0; i < PendingErrs.size(); ++i) {
            const MString& E = PendingErrs[i];
            lua_pushlstring(CoThread, E.c_str(), E.size());
            ++NArgsToPush;
        }
        for (size_t i = 0; i < PendingVals.size(); ++i) {
            const MString& V = PendingVals[i];
            lua_pushlstring(CoThread, V.c_str(), V.size());
            ++NArgsToPush;
        }

        int NResultsOut = 0;
        int RC          = lua_resume(CoThread, nullptr, NArgsToPush, &NResultsOut);

        // 清理 pending 队列
        PendingVals.clear();
        PendingErrs.clear();
        bResumePending = false;

        if (RC == LUA_OK) {
            bPending = false; // 协程正常结束
            // CoThread 已死但 registry ref 仍在,交给析构 unref
        } else if (RC == LUA_YIELD) {
            // 再次 yield:仍挂起,等下一次 resume
            bPending = true;
        } else {
            // 出错:视为完成(消费掉 pending)
            bPending = false;
            // 错误值留在栈顶,pop 掉以免污染后续调用
            if (lua_gettop(CoThread) > 0) {
                lua_pop(CoThread, 1);
            }
        }
        return RC;
    }

    // ----------------- FLuaPendingCallRegistry -----------------

    /**
     * @brief Register - 加锁登记并分配 LocalId。
     * @return 新 LocalId(从 1 起);Call 为空返回 0(不占用 id)
     */
    uint32 FLuaPendingCallRegistry::Register(TSharedPtr<FLuaPendingCall> Call) {
        if (!Call) {
            return 0;
        }
        std::lock_guard<std::mutex> Lock(Mutex);
        uint32                      Id = NextLocalId.fetch_add(1, std::memory_order_relaxed);
        Calls[Id]                      = std::move(Call);
        return Id;
    }

    void FLuaPendingCallRegistry::Unregister(uint32 LocalId) {
        std::lock_guard<std::mutex> Lock(Mutex);
        Calls.erase(LocalId);
    }

    TSharedPtr<FLuaPendingCall> FLuaPendingCallRegistry::Get(uint32 LocalId) const {
        std::lock_guard<std::mutex> Lock(Mutex);
        auto                        It = Calls.find(LocalId);
        if (It != Calls.end()) {
            return It->second;
        }
        return nullptr;
    }

    uint32 FLuaPendingCallRegistry::CountPending() const {
        std::lock_guard<std::mutex> Lock(Mutex);
        uint32                      N = 0;
        for (const auto& Kv : Calls) {
            if (Kv.second && Kv.second->IsPending()) {
                ++N;
            }
        }
        return N;
    }

    uint32 FLuaPendingCallRegistry::CountTotal() const {
        std::lock_guard<std::mutex> Lock(Mutex);
        return static_cast<uint32>(Calls.size());
    }

    /**
     * @brief StepAll - 快照后逐个 ResumeNow。
     * @return 本次被 resume 的调用数(与协程是否再次 yield 无关)
     *
     * @detail 先在锁内拷贝一份 TSharedPtr 列表(延长生命周期,避免 ResumeNow 期间
     *   表被并发改动),出锁后再执行,确保不持锁跑 Lua。
     */
    uint32 FLuaPendingCallRegistry::StepAll() {
        // 先在锁内快照,避免持锁执行 Lua
        TVector<TSharedPtr<FLuaPendingCall>> Snapshot;
        {
            std::lock_guard<std::mutex> Lock(Mutex);
            for (const auto& Kv : Calls) {
                if (Kv.second) {
                    Snapshot.push_back(Kv.second);
                }
            }
        }

        uint32 Resumed = 0;
        for (auto& Call : Snapshot) {
            if (Call && Call->HasResumeArgs()) {
                Call->ResumeNow();
                ++Resumed;
            }
        }
        return Resumed;
    }

} // namespace mession::script::lua