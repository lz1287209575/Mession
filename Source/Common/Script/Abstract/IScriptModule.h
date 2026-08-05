#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

namespace mession::script {

    // IScriptModule 把"类型注册"与"函数注册"拆两路
    // 便于 MActorRouter 在每条消息路径上复用已注册闭包
    // 每个 MClass 对应一个 module,registration 期不会互相覆盖
    class IScriptModule {
        public:
        virtual ~IScriptModule() = default;

        virtual void RegisterType(MClass* Cls)             = 0;
        virtual void RegisterFunction(MFunctionObject* Fn) = 0;
        virtual void RegisterProperty(MProperty* Prop)     = 0;

        virtual MClass* GetOwningClass() const = 0;
    };

} // namespace mession::script