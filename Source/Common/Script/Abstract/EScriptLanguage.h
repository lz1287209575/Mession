#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

namespace mession::script {

    enum class EScriptLanguage : uint8 {
        Lua        = 0,
        Python     = 1,
        TypeScript = 2,
        CSharp     = 3,
        CppRepl    = 4,
    };

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