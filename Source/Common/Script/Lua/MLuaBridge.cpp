/**
 * @file MLuaBridge.cpp
 * @brief 实现 MLuaBridge:M.InvokeStatic 的实参/返回值编解码与反射调用。
 */
#include "Common/Script/Lua/MLuaBridge.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Reflect/Class.h"
#include "Common/Runtime/Reflect/Property.h"
#include "Common/Runtime/Reflect/Reflection.h"
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

        /**
         * @brief FDeserializeFn - 把序列化字节流还原为某个 C++ 值的函数指针。
         */
        using FDeserializeFn = void (*)(const TByteArray& Data, void* OutValue);

        /**
         * @brief FDeserializerEntry - type_index → 反序列化函数 的注册项。
         *
         * @detail MLuaBridge 调 NativeInvoke 拿到 Out.Data(字节流)后需按返回类型还原;
         *   MProperty->CppTypeIndex 持有 typeid(T),但运行时无法对 type_index 做 switch,
         *   故启动时注册各 primitive 的还原函数,运行时按 type_index 查表。
         */
        struct FDeserializerEntry {
            std::type_index TypeIdx;
            FDeserializeFn  Fn;
        };

        /**
         * @brief GetDeserializerMap - 进程级反序列化注册表(函数内静态,首次调用时构造)。
         * @return 注册表引用;对同一 type 重复注册只是覆盖,无副作用
         */
        std::unordered_map<std::type_index, FDeserializeFn>& GetDeserializerMap() {
            static std::unordered_map<std::type_index, FDeserializeFn> Map;
            return Map;
        }

        /**
         * @brief RegisterDeserializerOnce - 若 T 尚未注册,则注册基于 MReflectArchive 的还原函数。
         * @tparam T 目标 C++ 类型
         */
        template <typename T> void RegisterDeserializerOnce() {
            auto&           Map = GetDeserializerMap();
            std::type_index Id(typeid(T));
            if (Map.find(Id) == Map.end()) {
                Map[Id] = [](const TByteArray& Data, void* OutValue) {
                    MReflectArchive Ar(Data); // 读模式
                    T               Val;
                    Ar << Val;
                    *static_cast<T*>(OutValue) = Val;
                };
            }
        }

        /** @brief RegisterAllDeserializers - 注册 int32 / int64 / double / bool / MString 五种还原函数 */
        void RegisterAllDeserializers() {
            RegisterDeserializerOnce<int32_t>();
            RegisterDeserializerOnce<int64_t>();
            RegisterDeserializerOnce<double>();
            RegisterDeserializerOnce<bool>();
            RegisterDeserializerOnce<MString>();
        }

        /**
         * @brief MarshalOne - 把 Lua 栈上一个值按 CppTypeIndex 写入 MReflectArchive。
         * @param L        目标 Lua 状态机
         * @param StackIdx 待读取的栈索引
         * @param TypeIdx  目标 C++ 类型(typeid 索引)
         * @param Out      输出归档(写入模式)
         * @return true 成功;false 表示 Lua 值类型与目标 C++ 类型不匹配
         */
        bool MarshalOne(lua_State* L, int StackIdx, const std::type_index& TypeIdx, MReflectArchive& Out) {
            if (TypeIdx == std::type_index(typeid(int32_t))) {
                auto R = PopInteger(L, StackIdx);
                if (R.IsErr())
                    return false;
                int32_t V = static_cast<int32_t>(R.GetValue());
                Out << V;
                return true;
            }
            if (TypeIdx == std::type_index(typeid(int64_t))) {
                auto R = PopInteger(L, StackIdx);
                if (R.IsErr())
                    return false;
                int64_t V = R.GetValue();
                Out << V;
                return true;
            }
            if (TypeIdx == std::type_index(typeid(double))) {
                auto R = PopNumber(L, StackIdx);
                if (R.IsErr())
                    return false;
                Out << R.GetValue();
                return true;
            }
            if (TypeIdx == std::type_index(typeid(bool))) {
                auto R = PopBoolean(L, StackIdx);
                if (R.IsErr())
                    return false;
                Out << R.GetValue();
                return true;
            }
            if (TypeIdx == std::type_index(typeid(MString))) {
                if (!IsString(L, StackIdx))
                    return false;
                size_t      Len = 0;
                const char* P   = lua_tolstring(L, StackIdx, &Len);
                MString     S(P, Len);
                Out << S;
                return true;
            }
            return false; // 不支持的类型
        }

        /**
         * @brief PushReturnOne - 把序列化字节流按 CppTypeIndex 还原并压入 Lua 栈。
         * @param L       目标 Lua 状态机
         * @param Data    序列化字节流(通常是 Out.Data)
         * @param TypeIdx 返回值 C++ 类型(typeid 索引)
         * @return 压入的值的个数(1);类型未注册时返回 -1
         *
         * @detail 返回值为 int64 时经 PushInteger 完整压栈(lua_Integer 为 64 位,无截断)。
         */
        int PushReturnOne(lua_State* L, const TByteArray& Data, const std::type_index& TypeIdx) {
            auto& Map = GetDeserializerMap();
            auto  It  = Map.find(TypeIdx);
            if (It == Map.end()) {
                return -1; // 不支持
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
                PushInteger(L, Val); // lua_Integer 为 64 位,完整保留 int64 值
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

        /**
         * @brief M_InvokeStatic - M.InvokeStatic 的 cfunction 实现体。
         * @param L 目标 Lua 状态机
         * @return 1(有返回值)/ 0(void 或出错);出错一律经 luaL_error 抛出
         *
         * @detail 入参栈约定: [1] ClassName(string) [2] MethodName(string) [3..] args;
         *   第 3 槽既可为逐个展开的变参,也可为一个 args 表。调用经反射现场构造
         *   临时实例,调用结束后 DestroyInstance。
         */
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
                    lua_geti(L, 3, T); // 依次压入 elem1..elemN,保持原顺序
                }
                lua_remove(L, 3); // 移除表本身,元素整体前移到槽 3..2+TLen
                NArgs = 2 + TLen;
            }

            // 校验 args 数量
            size_t Provided = (NArgs >= 2) ? (NArgs - 2) : 0;
            if (Provided != Fn->Params.size()) {
                // lua_pushfstring 仅支持 s/c/d/f/p 等有限格式,
                // 计数值很小,用 %d + int 强转安全。
                luaL_error(L, "arg_count_mismatch: %s.%s expected=%d got=%d", ClassName, MethodName, static_cast<int>(Fn->Params.size()), static_cast<int>(Provided));
                return 0;
            }

            // 1) Lua 实参 → MReflectArchive In
            MReflectArchive In;
            for (size_t i = 0; i < Fn->Params.size(); ++i) {
                const MProperty* P = Fn->Params[i];
                if (!P) {
                    luaL_error(L, "null_param_descriptor: %s.%s[%d]", ClassName, MethodName, static_cast<int>(i));
                    return 0;
                }
                int StackIdx = 3 + static_cast<int>(i);
                if (!MarshalOne(L, StackIdx, P->CppTypeIndex, In)) {
                    luaL_error(L, "arg_type_mismatch: %s.%s arg=%s", ClassName, MethodName, P->Name.c_str());
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
            void*           SelfObj = Cls->CreateInstance();
            MReflectArchive Out;
            bool            bInvoked = SelfObj && Fn->NativeInvoke(static_cast<MObject*>(SelfObj), &In, &Out);
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
                    luaL_error(L, "unsupported_return_type: %s.%s", ClassName, MethodName);
                    return 0;
                }
                return RC;
            }
            return 0; // void
        }

    } // namespace

    /**
     * @brief Install - 注册反序列化表,并把 M.InvokeStatic 挂到全局 M 表。
     * @param L 目标 Lua 状态机
     *
     * @detail M 表不存在时先建空表。NOTE: 形参 Engine 当前未被使用,
     *   M.GetObject / M.LiveObjects 也未注册。
     */
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