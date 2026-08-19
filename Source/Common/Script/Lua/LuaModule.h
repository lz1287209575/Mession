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

    class MLuaScriptState;

    class MLuaModule : public mession::script::IScriptModule {
        public:
        MLuaModule(lua_State* InL, MClass* InCls);
        ~MLuaModule() override;

        void RegisterType(MClass* Cls) override;
        void RegisterFunction(MFunctionObject* Fn) override;
        void RegisterProperty(MProperty* Prop) override;

        MClass* GetOwningClass() const override {
            return OwningClass;
        }

        void InstallIntoGlobal(const MString& GlobalName);

        // DualVM 热重载:T2 调用,T3 实现完整语义。
        // 当前是 no-op stub,只为了解除编译循环。
        void Rebind(MLuaScriptState& /*NewState*/) {
        }

        private:
        lua_State* L              = nullptr;
        MClass*    OwningClass    = nullptr;
        int32      ModuleTableRef = -1;
    };

} // namespace mession::script::lua