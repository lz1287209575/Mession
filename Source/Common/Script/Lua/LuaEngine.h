#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Abstract/EReloadMode.h"
#include "Common/Script/Abstract/EReloadResult.h"
#include "Common/Script/Abstract/EScriptLanguage.h"
#include "Common/Script/Abstract/IScriptEngine.h"
#include "Common/Script/Abstract/IScriptModule.h"
#include "Common/Script/Abstract/SScriptEngineConfig.h"
#include "Common/Script/Abstract/TVariant.h"
#include "Common/Script/Lua/LuaScriptState.h"

namespace mession::script::lua {

    // 前向声明(避免在 header 引入 FLuaPendingCall/MLuaProxyActor 全定义)
    class MLuaEngine;
    struct FLuaPendingCall;
    class MLuaProxyActor;

    class MLuaEngine : public mession::script::IScriptEngine {
        public:
        MLuaEngine();
        ~MLuaEngine() override;

        TResult<void> Initialize(const mession::script::SScriptEngineConfig& Cfg) override;
        void          Tick(float DeltaSeconds) override;
        void          StepCoroutines() override;
        TResult<void> Shutdown() override;

        // 内部:Hot reload 替换 State(Replace Old State with New State)
        // 旧 State 由调用方持有,负责 lua_close
        TUniquePtr<MLuaScriptState> ReplaceState(TUniquePtr<MLuaScriptState> NewState);

        // Hot reload 内部 helper:暴露旧 State 引用供 Drain / CountPendingCalls
        MLuaScriptState& GetStateForReload() {
            return *State;
        }

        // DualVM 计数器:每次 ReplaceState/BeginSwap 时 ++
        // TScriptInstanceHandle.Generation 持它创建时的值;跨 generation 调用立即 fail-fast
        uint32 GetVmGeneration() const {
            return VmGeneration;
        }

        // 测试 / 业务代码加载辅助:把 Lua 字节流加载到当前 State
        MString LoadBufferIntoState(const char* Name, const char* Bytes, size_t Size) {
            if (!State || !State->IsValid())
                return MString("engine_not_initialized");
            return State->LoadBuffer(Name, Bytes, Size);
        }

        TResult<EReloadResult> Reload(EReloadMode Mode) override;

        TResult<void> CallFunction(MFunctionObject* Fn, const mession::script::TScriptArgs& Args) override;
        TResult<void> CallFunctionById(uint64 FunctionId, const mession::script::TScriptArgs& Args) override;

        TResult<MObject*> CreateInstance(MClass* Cls, const mession::script::TScriptArgs& Args) override;
        TResult<uint64>   CreateActor(MClass* Cls, const mession::script::TScriptArgs& Args) override;

        TSharedPtr<mession::script::IScriptModule> CreateModule(MClass* OwningClass) override;
        void                                       RegisterModule(TSharedPtr<mession::script::IScriptModule> Module) override;
        void                                       UnregisterModule(MClass* OwningClass) override;

        // C++ 持有脚本侧 class 实例(跨 VM 通用)
        TResult<TScriptInstanceHandle> CreateInstanceByClassName(const MString& ClassName, const TScriptArgs& Args) override;
        TResult<TVariant>              InvokeInstanceMethod(TScriptInstanceHandle Handle, const MString& MethodName, const TScriptArgs& Args) override;
        void                           ReleaseInstance(TScriptInstanceHandle Handle) override;

        void                               SetGlobal(MStringView Key, const mession::script::TVariant& Value) override;
        TResult<mession::script::TVariant> GetGlobal(MStringView Key) override;

        MString GetErrorString(int32 State, void* Frame) const override;

        mession::script::EScriptLanguage GetLanguage() const override {
            return mession::script::EScriptLanguage::Lua;
        }
        bool IsSandboxed() const override {
            return false;
        }

        private:
        TUniquePtr<MLuaScriptState>                               State;
        TMap<MClass*, TSharedPtr<mession::script::IScriptModule>> Modules;
        uint32                                                    VmGeneration = 1;
    };

} // namespace mession::script::lua