#pragma once

#include "Common/Runtime/MLib.h"

namespace mession::script {

enum class EScriptLanguage : uint8
{
    Lua        = 0,
    Python     = 1,
    TypeScript = 2,
    CSharp     = 3,
    CppRepl    = 4,
};

inline const char* ScriptLanguageName(EScriptLanguage Lang)
{
    switch (Lang)
    {
    case EScriptLanguage::Lua:        return "Lua";
    case EScriptLanguage::Python:     return "Python";
    case EScriptLanguage::TypeScript: return "TypeScript";
    case EScriptLanguage::CSharp:     return "CSharp";
    case EScriptLanguage::CppRepl:    return "CppRepl";
    }
    return "Unknown";
}

} // namespace mession::script