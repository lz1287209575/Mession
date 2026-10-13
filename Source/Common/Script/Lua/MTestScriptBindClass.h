/**
 * @file MTestScriptBindClass.h
 * @brief MTestScriptBindClass —— 测试用反射类,走真实的 MHeaderTool codegen 流程。
 *
 * @detail 目的是用生产同款 codegen 路径验证 Lua → C++ 反射调用,不再手写注册:
 *   1. MHeaderTool 解析本文件,emit MTestScriptBindClass.mgenerated.{cpp,h} + .lua/.d.tl;
 *   2. 生成的 .cpp 在静态初始化期经 MREGISTER_NATIVE_METHOD 注册 NativeInvoke;
 *   3. 该生成 TU 反向 include 本头文件,测试调 StaticClass() 拿 MClass*;
 *   4. MLuaBridge::M_InvokeStatic 走 C++ 真反射 invoke。
 *
 * 命名用 ScriptBind 而非 LuaBind,因为后续还要接 Python / TS / C# 等同类入口,
 * 见 Docs/CodingStyle.md。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief MTestScriptBindClass - 4 个 ScriptBind 方法的测试载体。
 *
 * @detail 方法都注册成 ScriptBind 入口,由 M.InvokeStatic 经反射调用;反射层会为每次
 *   调用临时构造一个实例当 self(见 MLuaBridge::M_InvokeStatic),因此方法不需要
 *   依赖成员状态。
 */
MCLASS(Type = Service)
class MTestScriptBindClass : public MObject {
    public:
    MGENERATED_BODY(MTestScriptBindClass, MObject, 0)
    public:
    int32_t Value = 5; // 非反射成员(无 MPROPERTY),仅为占位

    // === 4 个 MFUNCTION(ScriptBind) 入口 ===

    /**
     * @brief Add - 整数相加。
     * @return A + B
     */
    MFUNCTION(ScriptBind)
    int32_t Add(int32_t A, int32_t B) {
        return A + B;
    }

    /**
     * @brief Scale - 按固定倍率放大。
     * @param X 输入值
     * @return X * 2.5
     */
    MFUNCTION(ScriptBind)
    double Scale(double X) {
        return X * 2.5;
    }

    /**
     * @brief Echo - 原样回显并追加 "!"。
     * @param S 输入字符串(按值传入)
     * @return S + "!"
     */
    MFUNCTION(ScriptBind)
    MString Echo(MString S) {
        return S + MString("!");
    }

    /**
     * @brief DoNothing - 空实现,用于验证 void 返回值路径。
     */
    MFUNCTION(ScriptBind)
    void DoNothing() {
    }
};