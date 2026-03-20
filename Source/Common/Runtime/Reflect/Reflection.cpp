#include "Common/Runtime/Reflect/Reflection.h"

MString MObject::ToString() const
{
    MClass* LocalClass = GetClass();
    if (!LocalClass)
    {
        return "MObject{Class=<null>, ObjectId=" + MStringUtil::ToString(GetObjectId()) + "}";
    }

    MString Body = LocalClass->ExportObjectToString(this);
    if (!Name.empty())
    {
        Body += " [Name=\"" + Name + "\"]";
    }
    Body += " [ObjectId=" + MStringUtil::ToString(GetObjectId()) + "]";
    return Body;
}

bool MObject::CallFunction(const MString& InName)
{
    return InvokeFunction<void>(InName, nullptr);
}
