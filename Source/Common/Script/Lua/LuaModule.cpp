#include "Common/Script/Lua/LuaModule.h"
#include "Common/Runtime/Reflect/Class.h"
#include "Common/Runtime/Reflect/Property.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    MLuaModule::MLuaModule(lua_State* InL, MClass* InCls) : L(InL), OwningClass(InCls) {
        lua_newtable(L);
        ModuleTableRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    MLuaModule::~MLuaModule() {
        if (L && ModuleTableRef != -1) {
            luaL_unref(L, LUA_REGISTRYINDEX, ModuleTableRef);
        }
    }

    void MLuaModule::RegisterType(MClass* Cls) {
        if (!Cls || Cls != OwningClass || !L)
            return;
        lua_geti(L, LUA_REGISTRYINDEX, ModuleTableRef);
        MString ClassName = Cls->GetName();
        lua_pushlstring(L, ClassName.c_str(), ClassName.size());
        lua_pushlstring(L, ClassName.c_str(), ClassName.size());
        lua_settable(L, -3);
        lua_pop(L, 1);
    }

    void MLuaModule::RegisterFunction(MFunctionObject* Fn) {
        if (!Fn || !L)
            return;
        lua_geti(L, LUA_REGISTRYINDEX, ModuleTableRef);
        MString FnName = Fn->Name;
        lua_pushlstring(L, FnName.c_str(), FnName.size());
        lua_pushcfunction(
            L, +[](lua_State* L) -> int { return luaL_error(L, "LuaBindEmitter not yet wired"); });
        lua_settable(L, -3);
        lua_pop(L, 1);
    }

    void MLuaModule::RegisterProperty(MProperty* Prop) {
        if (!Prop || !L)
            return;
        lua_geti(L, LUA_REGISTRYINDEX, ModuleTableRef);
        MString PropName = Prop->Name;
        lua_pushlstring(L, PropName.c_str(), PropName.size());
        lua_pushnil(L);
        lua_settable(L, -3);
        lua_pop(L, 1);
    }

    void MLuaModule::InstallIntoGlobal(const MString& GlobalName) {
        if (!L)
            return;
        lua_getglobal(L, "Mession");
        if (lua_type(L, -1) != LUA_TTABLE) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_setglobal(L, "Mession");
            lua_getglobal(L, "Mession");
        }
        lua_geti(L, LUA_REGISTRYINDEX, ModuleTableRef);
        lua_setfield(L, -2, GlobalName.c_str());
        lua_pop(L, 1);
    }

} // namespace mession::script::lua