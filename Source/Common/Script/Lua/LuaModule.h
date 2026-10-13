/**
 * @file LuaModule.h
 * @brief MLuaModule —— 把 MClass 反射信息镜像到 Lua 的 Mession.<GlobalName> 命名空间。
 *
 * @detail DualVM 兼容:每次 BeginSwap 之后引擎会调 Rebind(NewState),在新 VM 上重新
 *   luaL_ref 出 backing table 并重放已登记的类型 / 函数 / 属性列表。持有 Engine 指针
 *   (而不是 raw lua_State*)正是为了在 Rebind 时能拿到当前的活跃 VM。
 *
 * 线程约定:所有方法都假定在主 VM 线程调用,内部不加锁。
 */
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

    /**
     * @brief MLuaModule - 单个 MClass 对应的 Lua 侧模块表。
     *
     * @detail 底层是一张锚在 registry 里的 table(ModuleTableRef);InstallIntoGlobal 负责把它
     *   挂到 Mession.<GlobalName>。Lua 侧可见的函数目前都是 stub,一调就抛
     *   "LuaBindEmitter not yet wired";属性则填 nil。
     */
    class MLuaModule : public mession::script::IScriptModule {
        public:
        /** @brief 构造 - 直接用裸 lua_State*(旧签名,保留向后兼容) */
        MLuaModule(lua_State* InL, MClass* InCls);
        /**
         * @brief 构造 - 走 Engine(新代码用此签名)。
         *
         * @detail Engine 非空时才在活跃 VM 上创建 backing table;Engine 为空则退化为无表模块。
         */
        MLuaModule(MLuaEngine* InEngine, MString InGlobalName, MClass* InCls);
        /** @brief 析构 - unref 掉 ModuleTableRef(需要当时的活跃 VM 仍存活) */
        ~MLuaModule() override;

        void RegisterType(MClass* Cls) override;
        void RegisterFunction(MFunctionObject* Fn) override;
        void RegisterProperty(MProperty* Prop) override;

        MClass* GetOwningClass() const override {
            return OwningClass;
        }

        /**
         * @brief InstallIntoGlobal - 把本模块的表挂到 Mession.<GlobalName>。
         * @param GlobalName 命名空间键名(例 "Vector")
         *
         * @detail 会缓存 GlobalName,使后续 Rebind 能自动重建挂载点。
         *   Mession 全局表不存在时按需创建。
         */
        void InstallIntoGlobal(const MString& GlobalName);

        /**
         * @brief Rebind - DualVM 热重载:在新 VM 上重建表并重放全部已登记项。
         * @param NewState 新 VM
         *
         * @detail 步骤:重新 lua_newtable + luaL_ref → 重放 RegisteredTypes /
         *   RegisteredFunctions / RegisteredProperties → 若之前 InstallIntoGlobal 过,
         *   把表重新挂回 Mession.<GlobalName>。NewState 无 VM 时直接返回。
         */
        void Rebind(MLuaScriptState& NewState);

        private:
        /** @brief CurrentLuaState - 取当前活跃 VM;有 Engine 时优先问 Engine,否则用 fallback L */
        lua_State* CurrentLuaState() const;

        MLuaEngine* Engine      = nullptr; // 有它才能跟随 VM swap
        lua_State*  L           = nullptr; // fallback(无 Engine 时直接用)
        MClass*     OwningClass = nullptr; // 不拥有;指向反射系统的 MClass
        MString     GlobalName;            // 在 Mession 下的 namespace key(例 "Vector")
        int32       ModuleTableRef = -1;   // backing table 在 registry 里的 ref

        // Rebind 时用于重放的累积列表(按注册顺序)
        TVector<MString> RegisteredFunctions;
        TVector<MString> RegisteredProperties;
        TVector<MString> RegisteredTypes;
    };

} // namespace mession::script::lua