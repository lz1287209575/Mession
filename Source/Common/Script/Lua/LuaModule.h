#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Script/Abstract/IScriptModule.h"

extern "C" {
struct lua_State;
}

class MClass;
class MFunctionObject;
class MProperty;

namespace mession::script::lua {

    class MLuaEngine;
    class MLuaScriptState;

    // MLuaModule — 把 MClass 反射信息镜像到 Lua 端 Mession.<GlobalName> 命名空间下
    // DualVM 兼容:
    //   - Rebind(NewState) 在每次 BeginSwap 后被调,重新发 luaL_ref 并 replay
    //     RegisteredFunctions / RegisteredProperties / RegisteredTypes
    //   - Engine 指针让 Rebind 时可以拿到新的 lua_State(避免持有 raw L)
    class MLuaModule : public mession::script::IScriptModule {
        public:
        // 旧 ctor 保留(向后兼容);新代码用 (Engine, GlobalName, Class)
        MLuaModule(lua_State* InL, MClass* InCls);
        MLuaModule(MLuaEngine* InEngine, MString InGlobalName, MClass* InCls);
        ~MLuaModule() override;

        void RegisterType(MClass* Cls) override;
        void RegisterFunction(MFunctionObject* Fn) override;
        void RegisterProperty(MProperty* Prop) override;

        MClass* GetOwningClass() const override {
            return OwningClass;
        }

        void InstallIntoGlobal(const MString& GlobalName);

        // DualVM 热重载:在新 VM 上重新发 luaL_ref + 重放所有 Registered*
        void Rebind(MLuaScriptState& NewState);

        private:
        // 解 L — 优先用 Engine->GetStateForReload().GetLuaState() 拿当前活跃 VM
        lua_State* CurrentLuaState() const;

        MLuaEngine* Engine       = nullptr;
        lua_State*  L             = nullptr; // fallback(无 Engine 时直接用)
        MClass*     OwningClass   = nullptr;
        MString     GlobalName;            // 在 Mession 下的 namespace key(例 "Vector")
        int32       ModuleTableRef = -1;

        // Rebind replay 用的累积列表
        TVector<MString> RegisteredFunctions;
        TVector<MString> RegisteredProperties;
        TVector<MString> RegisteredTypes;
    };

} // namespace mession::script::lua