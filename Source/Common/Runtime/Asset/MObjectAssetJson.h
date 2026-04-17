#pragma once

#include "Common/Runtime/Json.h"

class MObject;

namespace MObjectAssetJson
{
bool ExportAssetObjectToJsonValue(const MObject* Object, MJsonValue& OutValue, MString* OutError = nullptr);
bool ExportAssetObjectToJson(const MObject* Object, MString& OutJson, MString* OutError = nullptr);

MObject* ImportAssetObjectFromJsonValue(const MJsonValue& InValue, MObject* Outer = nullptr, MString* OutError = nullptr);
MObject* ImportAssetObjectFromJson(const MString& InJson, MObject* Outer = nullptr, MString* OutError = nullptr);
}
