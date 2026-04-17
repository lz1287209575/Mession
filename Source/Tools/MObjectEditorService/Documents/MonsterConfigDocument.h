#pragma once

#include "Tools/MObjectEditorService/Core/EditorTypes.h"
#include "Tools/MObjectEditorService/Documents/IEditorDocument.h"
#include "Tools/MObjectEditorService/Models/MonsterConfigEditorModel.h"

class MMonsterConfigDocument : public IEditorDocument
{
public:
    bool CreateNew(const FEditorAssetIdentity& AssetId, MString& OutError);

    bool LoadFromFile(const MString& FilePath, MString& OutError) override;
    bool SaveToFile(const MString& FilePath, MString& OutError) override;

    bool Save(MString& OutError);

    bool IsDirty() const override { return bDirty; }
    void MarkDirty() override { bDirty = true; }
    void ClearDirty() override { bDirty = false; }

    FMonsterConfigEditorModel& GetModel() { return Model; }
    const FMonsterConfigEditorModel& GetModel() const { return Model; }
    void SetModel(const FMonsterConfigEditorModel& InModel, bool bMarkDirty = true);

    void SetIdentity(const FEditorAssetIdentity& InIdentity) { Identity = InIdentity; }
    const FEditorAssetIdentity& GetIdentity() const { return Identity; }

private:
    static bool ReadTextFile(const MString& FilePath, MString& OutText, MString& OutError);
    static bool WriteTextFile(const MString& FilePath, const MString& Text, MString& OutError);

    FEditorAssetIdentity Identity;
    FMonsterConfigEditorModel Model;
    bool bDirty = false;
};
