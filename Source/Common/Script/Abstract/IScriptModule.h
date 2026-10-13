/**
 * @file IScriptModule.h
 * @brief IScriptModule —— 把 MClass 反射信息注册到脚本侧的通道抽象。
 *
 * @detail 设计上把"类型注册"与"函数/属性注册"拆两路,以便按 MClass 粒度复用已注册的
 *   闭包:每个 MClass 对应一个 module,注册期互不覆盖。Lua 侧实现见 MLuaModule。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

namespace mession::script {

    /**
     * @brief IScriptModule - 单个 MClass 的脚本侧注册模块。
     */
    class IScriptModule {
        public:
        virtual ~IScriptModule() = default;

        /**
         * @brief RegisterType - 把类型名注册进脚本命名空间。
         * @param Cls 待注册的反射类;实现应忽略非自身 OwningClass 的入参
         */
        virtual void RegisterType(MClass* Cls) = 0;
        /**
         * @brief RegisterFunction - 注册一个可被脚本调用的函数描述符。
         * @param Fn 反射函数对象;所有权归反射系统,模块只读
         */
        virtual void RegisterFunction(MFunctionObject* Fn) = 0;
        /**
         * @brief RegisterProperty - 注册一个反射属性。
         * @param Prop 反射属性描述符;所有权归反射系统,模块只读
         */
        virtual void RegisterProperty(MProperty* Prop) = 0;

        /**
         * @brief GetOwningClass - 模块归属的 MClass。
         * @return 构造时绑定的 MClass 指针;可能为 nullptr
         */
        virtual MClass* GetOwningClass() const = 0;
    };

} // namespace mession::script