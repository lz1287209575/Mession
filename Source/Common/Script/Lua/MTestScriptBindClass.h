// MTestScriptBindClass — 测试用反射类(真用 MHeaderTool codegen)
//
// Phase 1: 静态 dispatch + 4 个 primitive args/return(self=nullptr)
// 流程:
//   1. mht 解析本文件,emit MTestScriptBindClass.mgenerated.cpp/.h + .lua/.d.tl
//   2. MTestScriptBindClass.mgenerated.cpp 调 MREGISTER_NATIVE_METHOD 在静态初始化时
//      注册 NativeInvoke(TNativeMethodInvoker 模板实例化)
//   3. TestLuaBind 链接 generated obj,调 MTestScriptBindClass::StaticClass() 拿 MClass*
//   4. MLuaBridge::M_InvokeStatic 走 C++ 真反射 invoke 路径
//
// 这个文件模拟真实业务:头里写注解,mht 跑 codegen,测试只 use。
// 不再有"手写 Register" — codegen 自动出。
// 命名从原 LuaBind 改成 ScriptBind,因为后续可能还要做 Python / TS / C#
// 同 Transport 入口,见 Docs/CodingStyle.md。

#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

MCLASS(Type = Service)
class MTestScriptBindClass : public MObject {
    public:
    MGENERATED_BODY(MTestScriptBindClass, MObject, 0)
    public:
    int32_t Value = 5;

    // === MFUNCTION(ScriptBind) 4 个静态方法 ===
    MFUNCTION(ScriptBind)
    int32_t Add(int32_t A, int32_t B) {
        return A + B;
    }

    MFUNCTION(ScriptBind)
    double Scale(double X) {
        return X * 2.5;
    }

    MFUNCTION(ScriptBind)
    MString Echo(MString S) {
        return S + MString("!");
    }

    MFUNCTION(ScriptBind)
    void DoNothing() {
        // 静态 void 返回
    }
};