/**
 * @file EScriptLanguage.h
 * @brief EScriptLanguage —— 脚本后端语言枚举 + 显示名查询(经 MENUM 反射注册)。
 *
 * @detail 目前只有 Lua 有实现(MLuaEngine);其余取值供配置与
 *   MObject::FindEnum("EScriptLanguage") 反射查询使用,尚无对应 Engine。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

namespace mession::script {

    /**
     * @brief EScriptLanguage - 脚本语言标识。
     */
    enum class EScriptLanguage : uint8 {
        Lua        = 0, // 已实现
        Python     = 1, // 预留
        TypeScript = 2, // 预留
        CSharp     = 3, // 预留
        CppRepl    = 4, // 预留:C++ REPL 后端
    };

    /**
     * @brief ScriptLanguageName - 枚举值 → 显示名。
     * @param Lang 脚本语言枚举
     * @return 指向静态字面量的只读字符串(进程级生命周期);未知值返回 "Unknown"
     */
    inline const char* ScriptLanguageName(EScriptLanguage Lang) {
        switch (Lang) {
        case EScriptLanguage::Lua:
            return "Lua";
        case EScriptLanguage::Python:
            return "Python";
        case EScriptLanguage::TypeScript:
            return "TypeScript";
        case EScriptLanguage::CSharp:
            return "CSharp";
        case EScriptLanguage::CppRepl:
            return "CppRepl";
        }
        return "Unknown";
    }

} // namespace mession::script

// 反射注册 — MHeaderTool 扫描 MENUM 宏 emit MHeaderTool_Generated_RegisterEnum_<Name>()
// 注册后 MObject::FindEnum("EScriptLanguage") 可查询
MENUM(EScriptLanguage)