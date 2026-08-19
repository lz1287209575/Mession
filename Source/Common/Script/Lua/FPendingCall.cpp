#include "Common/Script/Lua/FPendingCall.h"
#include "Common/Script/Lua/LuaCoroutineBridge.h"

#include <cassert>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    FLuaPendingCall::~FLuaPendingCall() {
        // 析构时如果还锚着 CoThread,unref 释放,允许 GC 回收
        if (MainL && ThreadRef != -1) {
            luaL_unref(MainL, LUA_REGISTRYINDEX, ThreadRef);
        }
    }

    void FLuaPendingCall::InitYield(lua_State* InMainL, lua_State* InCoThread, int32 InNArgs) {
        assert(InMainL && InCoThread);
        MainL      = InMainL;
        CoThread   = InCoThread;
        NArgs      = InNArgs;
        bPending   = true;

        // 锚 CoThread 到主 VM 的 registry,防 GC
        // 调用方已经把 CoThread push 在栈顶(通过 lua_newthread / lua_pushthread)
        lua_pushvalue(MainL, -1);
        ThreadRef = luaL_ref(MainL, LUA_REGISTRYINDEX);
    }

    void FLuaPendingCall::EnqueueResume(const MString* OutVals, size_t OutCount,
                                       const MString* OutErrs, size_t ErrCount) {
        if (!bPending) {
            return;  // 已经完成或被 cancel,丢弃 resume
        }
        // 把待 push 的参数 copy 到 Pending* 等 tick 时用
        PendingVals.assign(OutVals ? OutVals : nullptr,
                          OutVals ? OutVals + OutCount : nullptr);
        PendingErrs.assign(OutErrs ? OutErrs : nullptr,
                          OutErrs ? OutErrs + ErrCount : nullptr);
        bResumePending = true;
    }

    int FLuaPendingCall::ResumeNow() {
        if (!bPending || !CoThread || !MainL) {
            return -1; // LUA_ERR
        }

        // 先 push error args 到协程栈(如果有)
        // Lua resume 约定:协程 resume 时,栈上的值就是 args;
        // 我们先 push errors 作为 yield-resume 的 traceback,然后 push values
        // 简化版:仅 push values,errors 当 yield() 的第一个返回值(pcall 风格)
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
            bPending = false;  // 协程已正常结束
            // CoThread 可能已死,但 registry ref 还在;析构 unref 即可
        } else if (RC == LUA_YIELD) {
            // 再次 yield;仍 pending,等待下次 resume
            bPending = true;
        } else {
            // 错误;标记完成(consume the pending),记录日志
            bPending = false;
            // 错误信息留在协程栈顶,consume 掉
            if (lua_gettop(CoThread) > 0) {
                lua_pop(CoThread, 1);
            }
        }
        return RC;
    }

    // ----------------- FLuaPendingCallRegistry -----------------

    uint32 FLuaPendingCallRegistry::Register(TSharedPtr<FLuaPendingCall> Call) {
        if (!Call) {
            return 0;
        }
        std::lock_guard<std::mutex> Lock(Mutex);
        uint32 Id = NextLocalId.fetch_add(1, std::memory_order_relaxed);
        Calls[Id] = std::move(Call);
        return Id;
    }

    void FLuaPendingCallRegistry::Unregister(uint32 LocalId) {
        std::lock_guard<std::mutex> Lock(Mutex);
        Calls.erase(LocalId);
    }

    TSharedPtr<FLuaPendingCall> FLuaPendingCallRegistry::Get(uint32 LocalId) const {
        std::lock_guard<std::mutex> Lock(Mutex);
        auto It = Calls.find(LocalId);
        if (It != Calls.end()) {
            return It->second;
        }
        return nullptr;
    }

    uint32 FLuaPendingCallRegistry::CountPending() const {
        std::lock_guard<std::mutex> Lock(Mutex);
        uint32 N = 0;
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

    uint32 FLuaPendingCallRegistry::StepAll() {
        // snapshot pending + resume-pending 列表,避免在 mutex 内做长操作
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