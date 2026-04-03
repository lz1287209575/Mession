#pragma once

#include "Common/Runtime/MLib.h"
#include "Servers/Scene/Combat/SkillSpec.h"

class FUAssetSkillLoader
{
public:
    static bool LoadSkillSpecFromFile(
        const MString& FilePath,
        FSkillSpec& OutSkillSpec,
        MString& OutError);
};
