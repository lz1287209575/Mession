#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Common/ObjectCallMessages.h"

class MObjectCallRegistry;

class IObjectCallRootResolver
{
public:
    virtual ~IObjectCallRootResolver() = default;

    virtual EObjectCallRootType GetRootType() const = 0;
    virtual EServerType GetOwnerServerType() const = 0;
    virtual MObject* ResolveRootObject(uint64 RootId) const = 0;
};

class IObjectCallRegistryProvider
{
public:
    virtual ~IObjectCallRegistryProvider() = default;

    virtual MObjectCallRegistry* GetObjectCallRegistry() = 0;
    virtual const MObjectCallRegistry* GetObjectCallRegistry() const = 0;
};

class MObjectCallRegistry
{
public:
    void RegisterResolver(const IObjectCallRootResolver* Resolver);
    void UnregisterResolver(EObjectCallRootType RootType);

    const IObjectCallRootResolver* FindResolver(EObjectCallRootType RootType) const;
    EServerType ResolveOwnerServerType(EObjectCallRootType RootType) const;

    TResult<MObject*, FAppError> ResolveTargetObject(const FObjectCallTarget& Target) const;

private:
    TMap<EObjectCallRootType, const IObjectCallRootResolver*> Resolvers;
};
