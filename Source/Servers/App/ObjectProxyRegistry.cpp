#include "Servers/App/ObjectProxyRegistry.h"

#include "Common/Runtime/Object/ObjectDomainUtils.h"

void MObjectProxyRegistry::RegisterResolver(const IObjectProxyRootResolver* Resolver)
{
    if (!Resolver)
    {
        return;
    }

    Resolvers[Resolver->GetRootType()] = Resolver;
}

void MObjectProxyRegistry::UnregisterResolver(EObjectProxyRootType RootType)
{
    Resolvers.erase(RootType);
}

const IObjectProxyRootResolver* MObjectProxyRegistry::FindResolver(EObjectProxyRootType RootType) const
{
    const auto It = Resolvers.find(RootType);
    return It != Resolvers.end() ? It->second : nullptr;
}

EServerType MObjectProxyRegistry::ResolveOwnerServerType(EObjectProxyRootType RootType) const
{
    const IObjectProxyRootResolver* Resolver = FindResolver(RootType);
    return Resolver ? Resolver->GetOwnerServerType() : EServerType::Unknown;
}

TResult<MObject*, FAppError> MObjectProxyRegistry::ResolveTargetObject(const FObjectProxyTarget& Target) const
{
    if (Target.RootType == EObjectProxyRootType::Unknown)
    {
        return MakeErrorResult<MObject*>(FAppError::Make("object_proxy_root_type_required"));
    }

    if (Target.RootId == 0)
    {
        return MakeErrorResult<MObject*>(FAppError::Make("object_proxy_root_id_required"));
    }

    const IObjectProxyRootResolver* Resolver = FindResolver(Target.RootType);
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

    MObject* TargetObject = MObjectDomainUtils::ResolveObjectPath(RootObject, Target.ObjectPath);
    if (!TargetObject)
    {
        return MakeErrorResult<MObject*>(FAppError::Make(
            "object_proxy_path_not_found",
            Target.ObjectPath.c_str()));
    }

    return TResult<MObject*, FAppError>::Ok(TargetObject);
}
