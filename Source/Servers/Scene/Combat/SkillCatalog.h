#pragma once

#include "Common/Runtime/MLib.h"
#include "Servers/Scene/Combat/SkillSpec.h"

class MSkillCatalog
{
public:
    void LoadBuiltInDefaults();
    bool LoadFromDirectory(const MString& DirectoryPath, TVector<MString>* OutWarnings = nullptr);

    bool RegisterSkill(const FSkillSpec& SkillSpec, MString* OutError = nullptr);
    const FSkillSpec* FindSkill(uint32 SkillId) const;

private:
    TMap<uint32, FSkillSpec> SkillsById;
};
