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

    lua_State* MLuaModule::CurrentLuaState() const {
        if (Engine) {
            return Engine->GetStateForReload().GetLuaState();
        }
        return L;
    }

    MLuaModule::MLuaModule(lua_State* InL, MClass* InCls) : L(InL), OwningClass(InCls) {
        lua_State* Use = InL;
        lua_newtable(Use);
        ModuleTableRef = luaL_ref(Use, LUA_REGISTRYINDEX);
    }

    MLuaModule::MLuaModule(MLuaEngine* InEngine, MString InGlobalName, MClass* InCls)
        : Engine(InEngine), OwningClass(InCls), GlobalName(std::move(InGlobalName)) {
        if (Engine) {
            lua_State* Use = Engine->GetStateForReload().GetLuaState();
            L             = Use;
            lua_newtable(Use);
            ModuleTableRef = luaL_ref(Use, LUA_REGISTRYINDEX);
        }
    }

    MLuaModule::~MLuaModule() {
        lua_State* Use = CurrentLuaState();
        if (Use && ModuleTableRef != -1) {
            luaL_unref(Use, LUA_REGISTRYINDEX, ModuleTableRef);
        }
    }

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

    void MLuaModule::RegisterFunction(MFunctionObject* Fn) {
        if (!Fn)
            return;
        lua_State* Use = CurrentLuaState();
        if (!Use)
            return;
        MString FnName = Fn->Name;
        lua_geti(Use, LUA_REGISTRYINDEX, ModuleTableRef);
        lua_pushlstring(Use, FnName.c_str(), FnName.size());
        lua_pushcfunction(
            Use, +[](lua_State* L) -> int { return luaL_error(L, "LuaBindEmitter not yet wired"); });
        lua_settable(Use, -3);
        lua_pop(Use, 1);
        RegisteredFunctions.push_back(FnName);
    }

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

    void MLuaModule::InstallIntoGlobal(const MString& GlobalName) {
        lua_State* Use = CurrentLuaState();
        if (!Use)
            return;
        // 缓存传入的 GlobalName(让 Rebind 可以自动 Install)
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

    void MLuaModule::Rebind(MLuaScriptState& NewState) {
        lua_State* NewL = NewState.GetLuaState();
        if (!NewL)
            return;

        // 1. 在新 VM 上重新建 backing table,新 ModuleTableRef
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

        // 3. 重放 RegisteredFunctions(push stub cfunction,内容同 RegisterFunction)
        for (const MString& FnName : RegisteredFunctions) {
            lua_geti(NewL, LUA_REGISTRYINDEX, ModuleTableRef);
            lua_pushlstring(NewL, FnName.c_str(), FnName.size());
            lua_pushcfunction(
                NewL, +[](lua_State* L) -> int { return luaL_error(L, "LuaBindEmitter not yet wired"); });
            lua_settable(NewL, -3);
            lua_pop(NewL, 1);
        }

        // 4. 重放 RegisteredProperties(push nil — 同 stub 行为)
        for (const MString& PropName : RegisteredProperties) {
            lua_geti(NewL, LUA_REGISTRYINDEX, ModuleTableRef);
            lua_pushlstring(NewL, PropName.c_str(), PropName.size());
            lua_pushnil(NewL);
            lua_settable(NewL, -3);
            lua_pop(NewL, 1);
        }

        // 5. 如果之前 InstallIntoGlobal 过,把模块表挂回 Mession.<GlobalName>
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