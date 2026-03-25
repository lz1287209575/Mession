#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Common/ObjectProxyMessages.h"

class MObjectProxyRegistry;

class IObjectProxyRootResolver
{
public:
    virtual ~IObjectProxyRootResolver() = default;

    virtual EObjectProxyRootType GetRootType() const = 0;
    virtual EServerType GetOwnerServerType() const = 0;
    virtual MObject* ResolveRootObject(uint64 RootId) const = 0;
};

class IObjectProxyRegistryProvider
{
public:
    virtual ~IObjectProxyRegistryProvider() = default;

    virtual MObjectProxyRegistry* GetObjectProxyRegistry() = 0;
    virtual const MObjectProxyRegistry* GetObjectProxyRegistry() const = 0;
};

class MObjectProxyRegistry
{
public:
    void RegisterResolver(const IObjectProxyRootResolver* Resolver);
    void UnregisterResolver(EObjectProxyRootType RootType);

    const IObjectProxyRootResolver* FindResolver(EObjectProxyRootType RootType) const;
    EServerType ResolveOwnerServerType(EObjectProxyRootType RootType) const;

    TResult<MObject*, FAppError> ResolveTargetObject(const FObjectProxyTarget& Target) const;

private:
    TMap<EObjectProxyRootType, const IObjectProxyRootResolver*> Resolvers;
};
