// MTestBindClass — 测试用反射类(真用 MHeaderTool codegen)
//
// Phase 1: 静态 dispatch + 4 个 primitive args/return(self=nullptr)
// 流程:
//   1. mht 解析本文件,emit MTestBindClass.mgenerated.cpp/.h
//   2. MTestBindClass.mgenerated.cpp 调 MREGISTER_NATIVE_METHOD 在静态初始化时
//      注册 NativeInvoke(TNativeMethodInvoker 模板实例化)
//   3. TestLuaBind 链接生成 cpp,调 MTestBindClass::StaticClass() 拿 MClass*
//   4. MLuaBridge::M_InvokeStatic 走 C++ 真反射 invoke 路径
//
// 这个文件模拟真实业务:头里写注解,mht 跑 codegen,测试只 use。
// 不再有"手写 Register" — codegen 自动出。

#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

MCLASS(Type = Service)
class MTestBindClass : public MObject {
    public:
    MGENERATED_BODY(MTestBindClass, MObject, 0)
    public:
    int32_t Value = 5;

    // === MFUNCTION(LuaBind) 4 个静态方法 ===
    MFUNCTION(LuaBind)
    int32_t Add(int32_t A, int32_t B) {
        return A + B;
    }

    MFUNCTION(LuaBind)
    double Scale(double X) {
        return X * 2.5;
    }

    MFUNCTION(LuaBind)
    MString Echo(MString S) {
        return S + MString("!");
    }

    MFUNCTION(LuaBind)
    void DoNothing() {
        // 静态 void 返回
    }
};