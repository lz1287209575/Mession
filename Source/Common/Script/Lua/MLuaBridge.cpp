#include "Common/Script/Lua/MLuaBridge.h"
#include "Common/Runtime/Reflect/Class.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Reflect/Property.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Script/Lua/LuaEngine.h"
#include "Common/Script/Lua/LuaTypeBridge.h"

#include <cstring>
#include <typeindex>
#include <unordered_map>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace mession::script::lua {

    namespace {

        // ====================================================================
        // Deserializer map:CppTypeIndex (std::type_index) → 反序列化函数
        // ====================================================================
        //
        // MLuaBridge 调 NativeInvoke 拿到 Out.Data(byte stream),需要按返回类型
        // 反序列化成 C++ 值。MProperty->CppTypeIndex 持 typeid(T),但 runtime
        // 不能 switch on type_index。改成:启动时注册 4 个 primitive 的反序列化
        // 函数,运行时按 type_index 查表。
        using FDeserializeFn = void (*)(const TByteArray& Data, void* OutValue);

        struct FDeserializerEntry {
            std::type_index TypeIdx;
            FDeserializeFn  Fn;
        };

        // 全局注册表(进程级;同 type 多次注册覆盖即可,无副作用)
        std::unordered_map<std::type_index, FDeserializeFn>& GetDeserializerMap() {
            static std::unordered_map<std::type_index, FDeserializeFn> Map;
            return Map;
        }

        template <typename T>
        void RegisterDeserializerOnce() {
            auto& Map = GetDeserializerMap();
            std::type_index Id(typeid(T));
            if (Map.find(Id) == Map.end()) {
                Map[Id] = [](const TByteArray& Data, void* OutValue) {
                    MReflectArchive Ar(Data);  // read mode
                    T Val;
                    Ar << Val;
                    *static_cast<T*>(OutValue) = Val;
                };
            }
        }

        void RegisterAllDeserializers() {
            RegisterDeserializerOnce<int32_t>();
            RegisterDeserializerOnce<int64_t>();
            RegisterDeserializerOnce<double>();
            RegisterDeserializerOnce<bool>();
            RegisterDeserializerOnce<MString>();
        }

        // ====================================================================
        // Lua stack 1 个值 → MReflectArchive 1 个值(按 MProperty->CppTypeIndex 路由)
        // ====================================================================
        //
        // 返回 true 成功;false 表示 Lua 值类型与 C++ 类型不匹配
        bool MarshalOne(lua_State* L, int StackIdx, const std::type_index& TypeIdx, MReflectArchive& Out) {
            if (TypeIdx == std::type_index(typeid(int32_t))) {
                auto R = PopInteger(L, StackIdx);
                if (R.IsErr()) return false;
                int32_t V = static_cast<int32_t>(R.GetValue());
                Out << V;
                return true;
            }
            if (TypeIdx == std::type_index(typeid(int64_t))) {
                auto R = PopInteger(L, StackIdx);
                if (R.IsErr()) return false;
                int64_t V = R.GetValue();
                Out << V;
                return true;
            }
            if (TypeIdx == std::type_index(typeid(double))) {
                auto R = PopNumber(L, StackIdx);
                if (R.IsErr()) return false;
                Out << R.GetValue();
                return true;
            }
            if (TypeIdx == std::type_index(typeid(bool))) {
                auto R = PopBoolean(L, StackIdx);
                if (R.IsErr()) return false;
                Out << R.GetValue();
                return true;
            }
            if (TypeIdx == std::type_index(typeid(MString))) {
                if (!IsString(L, StackIdx)) return false;
                size_t      Len = 0;
                const char* P = lua_tolstring(L, StackIdx, &Len);
                MString S(P, Len);
                Out << S;
                return true;
            }
            return false;  // 不支持的类型
        }

        // ====================================================================
        // Out.Data(序列化 byte 流)→ Lua stack push(按 CppTypeIndex 路由)
        // ====================================================================
        //
        // 返回 push 的值个数(0 = void,1 = 1 个值);-1 表示错误
        int PushReturnOne(lua_State* L, const TByteArray& Data, const std::type_index& TypeIdx) {
            auto& Map = GetDeserializerMap();
            auto It = Map.find(TypeIdx);
            if (It == Map.end()) {
                return -1;  // 不支持
            }

            // 4 个 primitive 都小 — 直接 stack alloc
            if (TypeIdx == std::type_index(typeid(int32_t))) {
                int32_t Val = 0;
                It->second(Data, &Val);
                PushInteger(L, Val);
                return 1;
            }
            if (TypeIdx == std::type_index(typeid(int64_t))) {
                int64_t Val = 0;
                It->second(Data, &Val);
                PushInteger(L, static_cast<int32_t>(Val));  // Lua 5.4 默认 integer
                return 1;
            }
            if (TypeIdx == std::type_index(typeid(double))) {
                double Val = 0;
                It->second(Data, &Val);
                PushNumber(L, Val);
                return 1;
            }
            if (TypeIdx == std::type_index(typeid(bool))) {
                bool Val = false;
                It->second(Data, &Val);
                PushBoolean(L, Val);
                return 1;
            }
            if (TypeIdx == std::type_index(typeid(MString))) {
                MString Val;
                It->second(Data, &Val);
                PushString(L, Val);
                return 1;
            }
            return -1;
        }

        // ====================================================================
        // M_InvokeStatic(L) — cfunction body
        //   栈: [1] ClassName (string) [2] MethodName (string) [3..] args
        //   返: 1 个返回值,或 0(void),或 push error string
        // ====================================================================
        int M_InvokeStatic(lua_State* L) {
            int NArgs = lua_gettop(L);
            if (NArgs < 2) {
                luaL_error(L, "InvokeStatic: expected (ClassName, MethodName, ...)");
                return 0;
            }
            if (!IsString(L, 1) || !IsString(L, 2)) {
                luaL_error(L, "InvokeStatic: ClassName and MethodName must be strings");
                return 0;
            }

            const char* ClassName  = lua_tostring(L, 1);
            const char* MethodName = lua_tostring(L, 2);

            MClass* Cls = MObject::FindClass(ClassName);
            if (!Cls) {
                luaL_error(L, "class_not_found: %s", ClassName);
                return 0;
            }
            MFunctionObject* Fn = Cls->FindFunction(MethodName);
            if (!Fn) {
                luaL_error(L, "method_not_found: %s.%s", ClassName, MethodName);
                return 0;
            }
            if (!Fn->NativeInvoke) {
                luaL_error(L, "no_native_invoker: %s.%s", ClassName, MethodName);
                return 0;
            }

            // 两种传参形态都支持:
            //   M.InvokeStatic("Cls", "Fn", 2, 3)        — 变参展开
            //   M.InvokeStatic("Cls", "Fn", {2, 3})      — 单个 args 表(约定见 MLuaBridge.h)
            if (NArgs == 3 && lua_istable(L, 3)) {
                int TLen = static_cast<int>(lua_rawlen(L, 3));
                for (int T = 1; T <= TLen; ++T) {
                    lua_geti(L, 3, T);  // 依次压入 elem1..elemN,保持原顺序
                }
                lua_remove(L, 3);  // 移除表本身,元素整体前移到槽 3..2+TLen
                NArgs = 2 + TLen;
            }

            // 校验 args 数量
            size_t Provided = (NArgs >= 2) ? (NArgs - 2) : 0;
            if (Provided != Fn->Params.size()) {
                // lua_pushfstring 仅支持 s/c/d/f/p 等有限格式,
                // 计数值很小,用 %d + int 强转安全。
                luaL_error(L, "arg_count_mismatch: %s.%s expected=%d got=%d",
                           ClassName, MethodName,
                           static_cast<int>(Fn->Params.size()),
                           static_cast<int>(Provided));
                return 0;
            }

            // 1) Lua args → MReflectArchive In
            MReflectArchive In;
            for (size_t i = 0; i < Fn->Params.size(); ++i) {
                const MProperty* P = Fn->Params[i];
                if (!P) {
                    luaL_error(L, "null_param_descriptor: %s.%s[%d]",
                               ClassName, MethodName, static_cast<int>(i));
                    return 0;
                }
                int StackIdx = 3 + static_cast<int>(i);
                if (!MarshalOne(L, StackIdx, P->CppTypeIndex, In)) {
                    luaL_error(L, "arg_type_mismatch: %s.%s arg=%s",
                               ClassName, MethodName, P->Name.c_str());
                    return 0;
                }
            }

            // 1.5) In 切到读模式:NativeInvoke 内部用 Ar << Arg 做
            //      读写一体反序列化,写模式的 In 会把参数当新数据追加,
            //      导致方法收到全 0 参数(契约见 MReflectArchive 构造)。
            In.bWriting = false;
            In.bReading = true;
            In.ReadPos  = 0;

            // 2) NativeInvoke — ScriptBind 方法注册为成员函数指针,
            //    需要一个实例:每次调用经反射构造临时对象(self 生命周期
            //    仅覆盖本次调用),Phase 2 的 M.GetObject 再引入常驻实例。
            void*          SelfObj = Cls->CreateInstance();
            MReflectArchive Out;
            bool bInvoked = SelfObj && Fn->NativeInvoke(static_cast<MObject*>(SelfObj), &In, &Out);
            if (SelfObj) {
                Cls->DestroyInstance(SelfObj);
            }
            if (!bInvoked) {
                luaL_error(L, "native_invoke_failed: %s.%s", ClassName, MethodName);
                return 0;
            }

            // 3) Push 返回值
            if (Fn->ReturnProperty) {
                int RC = PushReturnOne(L, Out.Data, Fn->ReturnProperty->CppTypeIndex);
                if (RC < 0) {
                    luaL_error(L, "unsupported_return_type: %s.%s",
                               ClassName, MethodName);
                    return 0;
                }
                return RC;
            }
            return 0;  // void
        }

    } // namespace

    void MLuaBridge::Install(lua_State* L, MLuaEngine* /*Engine*/) {
        // 注册 deserializer 表(进程级;同 type 多次注册无害)
        RegisterAllDeserializers();

        // 注册 M_InvokeStatic 到 M 表
        lua_getglobal(L, "M");
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_setglobal(L, "M");
            lua_getglobal(L, "M");
        }
        lua_pushcfunction(L, M_InvokeStatic);
        lua_setfield(L, -2, "InvokeStatic");
        lua_pop(L, 1);

        // Phase 1 简化:GetObject 不实现(LiveObjects 暂未用)
        // 后续 Phase 2 加 M_GetObject + M.LiveObjects 注册
    }

} // namespace mession::script::lua