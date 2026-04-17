#pragma once

#include "Common/Runtime/MLib.h"

class IEditorDocument
{
public:
    virtual ~IEditorDocument() = default;

    virtual bool LoadFromFile(const MString& FilePath, MString& OutError) = 0;
    virtual bool SaveToFile(const MString& FilePath, MString& OutError) = 0;

    virtual bool IsDirty() const = 0;
    virtual void MarkDirty() = 0;
    virtual void ClearDirty() = 0;
};
