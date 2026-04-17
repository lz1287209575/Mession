#pragma once

#include "Common/Runtime/MLib.h"

class MObject;

namespace MObjectAssetLoader
{
MObject* LoadFromBytes(const TByteArray& Bytes, MObject* Outer = nullptr, MString* OutError = nullptr);
MObject* LoadFromFile(const MString& FilePath, MObject* Outer = nullptr, MString* OutError = nullptr);
}
