#include "Servers/App/ObjectCallRegistry.h"

#include "Common/Runtime/Object/ObjectDomainUtils.h"
#include "Common/Runtime/StringUtils.h"

namespace
{
MString NormalizeObjectPath(const MString& InPath)
{
    if (InPath.empty())
    {
        return {};
    }

    MString Path = MStringUtil::TrimCopy(InPath);
    for (char& Ch : Path)
    {
        if (Ch == '/' || Ch == '\\')
        {
            Ch = '.';
        }
    }

    TVector<MString> Segments = MStringUtil::Split(Path, '.');
    TVector<MString> NormalizedSegments;
    NormalizedSegments.reserve(Segments.size());
    for (MString& Segment : Segments)
    {
        MStringUtil::TrimInPlace(Segment);
        if (!Segment.empty())
        {
            NormalizedSegments.push_back(std::move(Segment));
        }
    }

    return MStringUtil::Join(NormalizedSegments, '.');
}

MString NormalizeIdentifier(const MString& InValue)
{
    MString Result;
    Result.reserve(InValue.size());
    for (char Ch : InValue)
    {
        if (Ch == ' ' || Ch == '_' || Ch == '-' || Ch == '.')
        {
            continue;
        }

        if (Ch >= 'A' && Ch <= 'Z')
        {
            Result.push_back(static_cast<char>(Ch - 'A' + 'a'));
        }
        else
        {
            Result.push_back(Ch);
        }
    }
    return Result;
}

MString BuildAvailableChildrenHint(const MObject* Object)
{
    if (!Object)
    {
        return {};
    }

    TVector<MString> Names;
    for (MObject* Child : Object->GetChildren())
    {
        if (!Child || Child->GetName().empty())
        {
            continue;
        }

        Names.push_back(Child->GetName());
    }

    return MStringUtil::Join(Names, ',');
}

MObject* FindChildBySegment(MObject* Parent, const MString& Segment)
{
    if (!Parent || Segment.empty())
    {
        return nullptr;
    }

    const MString NormalizedSegment = NormalizeIdentifier(Segment);
    for (MObject* Child : Parent->GetChildren())
    {
        if (!Child)
        {
            continue;
        }

        if (Child->GetName() == Segment)
        {
            return Child;
        }

        if (NormalizeIdentifier(Child->GetName()) == NormalizedSegment)
        {
            return Child;
        }
    }

    return nullptr;
}

TResult<MObject*, FAppError> ResolveTargetObjectPath(MObject* RootObject, const MString& InPath)
{
    if (!RootObject)
    {
        return MakeErrorResult<MObject*>(FAppError::Make("object_proxy_root_not_found"));
    }

    const MString Path = NormalizeObjectPath(InPath);
    if (Path.empty())
    {
        return TResult<MObject*, FAppError>::Ok(RootObject);
    }

    MObject* Current = RootObject;
    MString CurrentPath;
    for (const MString& Segment : MStringUtil::Split(Path, '.'))
    {
        if (Segment.empty())
        {
            continue;
        }

        MObject* Next = FindChildBySegment(Current, Segment);
        if (!Next)
        {
            MString Message = "path=" + Path + ", missing_segment=" + Segment;
            if (!CurrentPath.empty())
            {
                Message += ", resolved_prefix=" + CurrentPath;
            }

            const MString Available = BuildAvailableChildrenHint(Current);
            if (!Available.empty())
            {
                Message += ", available_children=" + Available;
            }

            return MakeErrorResult<MObject*>(FAppError::Make(
                "object_proxy_path_not_found",
                Message.c_str()));
        }

        Current = Next;
        CurrentPath = CurrentPath.empty()
            ? Current->GetName()
            : (CurrentPath + "." + Current->GetName());
    }

    return TResult<MObject*, FAppError>::Ok(Current);
}
} // namespace

void MObjectCallRegistry::RegisterResolver(const IObjectCallRootResolver* Resolver)
{
    if (!Resolver)
    {
        return;
    }

    Resolvers[Resolver->GetRootType()] = Resolver;
}

void MObjectCallRegistry::UnregisterResolver(EObjectCallRootType RootType)
{
    Resolvers.erase(RootType);
}

const IObjectCallRootResolver* MObjectCallRegistry::FindResolver(EObjectCallRootType RootType) const
{
    const auto It = Resolvers.find(RootType);
    return It != Resolvers.end() ? It->second : nullptr;
}

EServerType MObjectCallRegistry::ResolveOwnerServerType(EObjectCallRootType RootType) const
{
    const IObjectCallRootResolver* Resolver = FindResolver(RootType);
    return Resolver ? Resolver->GetOwnerServerType() : EServerType::Unknown;
}

TResult<MObject*, FAppError> MObjectCallRegistry::ResolveTargetObject(const FObjectCallTarget& Target) const
{
    if (Target.RootType == EObjectCallRootType::Unknown)
    {
        return MakeErrorResult<MObject*>(FAppError::Make("object_proxy_root_type_required"));
    }

    if (Target.RootId == 0)
    {
        return MakeErrorResult<MObject*>(FAppError::Make("object_proxy_root_id_required"));
    }

    const IObjectCallRootResolver* Resolver = FindResolver(Target.RootType);
    if (!Resolver)
    {
        return MakeErrorResult<MObject*>(FAppError::Make(
            "object_proxy_root_type_unsupported",
            std::to_string(static_cast<uint8>(Target.RootType))));
    }

    const EServerType OwnerServerType = Resolver->GetOwnerServerType();
    if (Target.TargetServerType != EServerType::Unknown &&
        OwnerServerType != EServerType::Unknown &&
        Target.TargetServerType != OwnerServerType)
    {
        return MakeErrorResult<MObject*>(FAppError::Make(
            "object_proxy_target_server_mismatch",
            std::to_string(static_cast<uint8>(Target.TargetServerType))));
    }

    MObject* RootObject = Resolver->ResolveRootObject(Target.RootId);
    if (!RootObject)
    {
        return MakeErrorResult<MObject*>(FAppError::Make(
            "object_proxy_root_not_found",
            std::to_string(Target.RootId)));
    }

    return ResolveTargetObjectPath(RootObject, Target.ObjectPath);
}
