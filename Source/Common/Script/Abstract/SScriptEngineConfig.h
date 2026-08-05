#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Script/Abstract/EScriptLanguage.h"

namespace mession::script {

struct SScriptEngineConfig
{
    EScriptLanguage Language   = EScriptLanguage::Lua;
    MString         VmName     = "default";
    MString         ReflectRootPath;        // Build/Generated/{vm}/ — MHeaderTool emit 根
    float           TickHz                = 60.0f;
    bool            bEnableDebugPort      = false;
    uint16          DebugPort             = 0;
    bool            bHotReloadOnFileChange = true;
};

} // namespace mession::script