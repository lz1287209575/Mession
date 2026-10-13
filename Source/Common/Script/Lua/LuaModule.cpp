/**
 * @file LuaModule.cpp
 * @brief 实现 MClass 反射信息到 Lua 模块表的镜像、挂载与热重载重放。
 */
#include "Common/Script/Lua/LuaModule.h"
#include "Common/Runtime/Reflect/Class.h"
#include "Common/Runtime/Reflect/Property.h"
#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Script/Lua/LuaScriptState.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    /**
     * @brief CurrentLuaState - 解出当前应操作的 VM。
     * @return 有 Engine 时取 Engine 的活跃 VM,否则返回构造时记下的 fallback L
     *
     * @detail 每次都重新问 Engine,因此 VM swap 后自动指向新 VM。
     */
    lua_State* MLuaModule::CurrentLuaState() const {
        if (Engine) {
            return Engine->GetStateForReload().GetLuaState();
        }
        return L;
    }

    /**
     * @brief 构造(旧签名)- 在给定 VM 上新建空 backing table。
     *
     * @detail 表本身不保存值,只作为 Mession.<GlobalName> 的载体。
     */
    MLuaModule::MLuaModule(lua_State* InL, MClass* InCls) : L(InL), OwningClass(InCls) {
        lua_State* Use = InL;
        lua_newtable(Use);
        ModuleTableRef = luaL_ref(Use, LUA_REGISTRYINDEX);
    }

    MLuaModule::MLuaModule(MLuaEngine* InEngine, MString InGlobalName, MClass* InCls) : Engine(InEngine), OwningClass(InCls), GlobalName(std::move(InGlobalName)) {
        if (Engine) {
            lua_State* Use = Engine->GetStateForReload().GetLuaState();
            L              = Use;
            lua_newtable(Use);
            ModuleTableRef = luaL_ref(Use, LUA_REGISTRYINDEX);
        }
    }

    /**
     * @brief 析构 - unref backing table。
     *
     * @detail 前提是当时的活跃 VM 仍存活;若 VM 已先一步销毁,CurrentLuaState 会返回
     *   失效指针,此处未做额外防护。
     */
    MLuaModule::~MLuaModule() {
        lua_State* Use = CurrentLuaState();
        if (Use && ModuleTableRef != -1) {
            luaL_unref(Use, LUA_REGISTRYINDEX, ModuleTableRef);
        }
    }

    /**
     * @brief RegisterType - 把类名写进模块表(键值同为类名)。
     * @param Cls 待注册的类;非本模块的 OwningClass 会被静默忽略
     *
     * @detail 同时记入 RegisteredTypes,供 Rebind 重放。
     */
    void MLuaModule::RegisterType(MClass* Cls) {
        if (!Cls || Cls != OwningClass)
            return;
        lua_State* Use = CurrentLuaState();
        if (!Use)
            return;
        MString ClassName = Cls->GetName();
        lua_geti(Use, LUA_REGISTRYINDEX, ModuleTableRef);
        lua_pushlstring(Use, ClassName.c_str(), ClassName.size());
        lua_pushlstring(Use, ClassName.c_str(), ClassName.size());
        lua_settable(Use, -3);
        lua_pop(Use, 1);
        RegisteredTypes.push_back(ClassName);
    }

    /**
     * @brief RegisterFunction - 把函数名登记为 stub cfunction。
     * @param Fn 反射函数;仅取 Name 作为键
     *
     * @detail 值是一个固定抛错的 cfunction("LuaBindEmitter not yet wired")——
     *   真正的绑定代码生成尚未接入。函数名记入 RegisteredFunctions 供 Rebind 重放。
     */
    void MLuaModule::RegisterFunction(MFunctionObject* Fn) {
        if (!Fn)
            return;
        lua_State* Use = CurrentLuaState();
        if (!Use)
            return;
        MString FnName = Fn->Name;
        lua_geti(Use, LUA_REGISTRYINDEX, ModuleTableRef);
        lua_pushlstring(Use, FnName.c_str(), FnName.size());
        lua_pushcfunction(Use, +[](lua_State* L) -> int { return luaL_error(L, "LuaBindEmitter not yet wired"); });
        lua_settable(Use, -3);
        lua_pop(Use, 1);
        RegisteredFunctions.push_back(FnName);
    }

    /**
     * @brief RegisterProperty - 把属性名登记为 nil 占位。
     * @param Prop 反射属性;仅取 Name 作为键
     *
     * @detail 属性名记入 RegisteredProperties 供 Rebind 重放;值恒为 nil。
     */
    void MLuaModule::RegisterProperty(MProperty* Prop) {
        if (!Prop)
            return;
        lua_State* Use = CurrentLuaState();
        if (!Use)
            return;
        MString PropName = Prop->Name;
        lua_geti(Use, LUA_REGISTRYINDEX, ModuleTableRef);
        lua_pushlstring(Use, PropName.c_str(), PropName.size());
        lua_pushnil(Use);
        lua_settable(Use, -3);
        lua_pop(Use, 1);
        RegisteredProperties.push_back(PropName);
    }

    /**
     * @brief InstallIntoGlobal - 把模块表挂到 Mession.<GlobalName>。
     * @param GlobalName 命名空间键名
     *
     * @detail 会缓存 GlobalName 供 Rebind 自动重挂。Mession 表不存在时按需创建。
     */
    void MLuaModule::InstallIntoGlobal(const MString& GlobalName) {
        lua_State* Use = CurrentLuaState();
        if (!Use)
            return;
        // 缓存键名,让 Rebind 能自动重挂
        this->GlobalName = GlobalName;

        lua_getglobal(Use, "Mession");
        if (lua_type(Use, -1) != LUA_TTABLE) {
            lua_pop(Use, 1);
            lua_newtable(Use);
            lua_setglobal(Use, "Mession");
            lua_getglobal(Use, "Mession");
        }
        lua_geti(Use, LUA_REGISTRYINDEX, ModuleTableRef);
        lua_setfield(Use, -2, GlobalName.c_str());
        lua_pop(Use, 1);
    }

    /**
     * @brief Rebind - 在新 VM 上重建模块表并重放注册记录。
     * @param NewState 新 VM;无 VM 时直接返回
     *
     * @detail 旧 VM 上的 ModuleTableRef 不再有效,这里直接覆盖成员(旧 ref 随旧 VM 一起消亡)。
     *   重放的顺序与内容必须与 RegisterXxx 保持一致,否则 Lua 侧可见的表结构会与
     *   首次装配时不同。
     */
    void MLuaModule::Rebind(MLuaScriptState& NewState) {
        lua_State* NewL = NewState.GetLuaState();
        if (!NewL)
            return;

        // 1. 在新 VM 上重建 backing table,拿到新的 ModuleTableRef
        lua_newtable(NewL);
        ModuleTableRef = luaL_ref(NewL, LUA_REGISTRYINDEX);

        // 2. 重放 RegisteredTypes
        for (const MString& ClassName : RegisteredTypes) {
            lua_geti(NewL, LUA_REGISTRYINDEX, ModuleTableRef);
            lua_pushlstring(NewL, ClassName.c_str(), ClassName.size());
            lua_pushlstring(NewL, ClassName.c_str(), ClassName.size());
            lua_settable(NewL, -3);
            lua_pop(NewL, 1);
        }

        // 3. 重放 RegisteredFunctions(仍是 stub cfunction,与 RegisterFunction 一致)
        for (const MString& FnName : RegisteredFunctions) {
            lua_geti(NewL, LUA_REGISTRYINDEX, ModuleTableRef);
            lua_pushlstring(NewL, FnName.c_str(), FnName.size());
            lua_pushcfunction(NewL, +[](lua_State* L) -> int { return luaL_error(L, "LuaBindEmitter not yet wired"); });
            lua_settable(NewL, -3);
            lua_pop(NewL, 1);
        }

        // 4. 重放 RegisteredProperties(仍为 nil,与 RegisterProperty 一致)
        for (const MString& PropName : RegisteredProperties) {
            lua_geti(NewL, LUA_REGISTRYINDEX, ModuleTableRef);
            lua_pushlstring(NewL, PropName.c_str(), PropName.size());
            lua_pushnil(NewL);
            lua_settable(NewL, -3);
            lua_pop(NewL, 1);
        }

        // 5. 之前 InstallIntoGlobal 过则把表重挂回 Mession.<GlobalName>
        if (!GlobalName.empty()) {
            lua_getglobal(NewL, "Mession");
            if (lua_type(NewL, -1) != LUA_TTABLE) {
                lua_pop(NewL, 1);
                lua_newtable(NewL);
                lua_setglobal(NewL, "Mession");
                lua_getglobal(NewL, "Mession");
            }
            lua_geti(NewL, LUA_REGISTRYINDEX, ModuleTableRef);
            lua_setfield(NewL, -2, GlobalName.c_str());
            lua_pop(NewL, 1);
        }
    }

} // namespace mession::script::lua