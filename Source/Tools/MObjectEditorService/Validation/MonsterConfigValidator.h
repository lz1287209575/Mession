#pragma once

#include "Tools/MObjectEditorService/Core/EditorTypes.h"
#include "Tools/MObjectEditorService/Models/MonsterConfigEditorModel.h"

class MMonsterConfigValidator
{
public:
    static TVector<FValidationIssue> Validate(const FMonsterConfigEditorModel& Model);
};
