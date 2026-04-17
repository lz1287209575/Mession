#pragma once

#include "Common/Runtime/MLib.h"

class MObject;

namespace MObjectAssetCompiler
{
bool CompileBytesFromObject(const MObject* RootObject, TByteArray& OutBytes, MString* OutError = nullptr);
bool CompileBytesFromJson(const MString& JsonText, TByteArray& OutBytes, MString* OutError = nullptr);

bool CompileFileFromJson(const MString& JsonFilePath, const MString& OutputFilePath, MString* OutError = nullptr);
}
