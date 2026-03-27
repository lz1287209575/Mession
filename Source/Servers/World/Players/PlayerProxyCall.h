#pragma once

#include "Protocol/Messages/Common/ObjectProxyMessages.h"
#include "Servers/App/ObjectProxyCall.h"

namespace MPlayerProxyCall
{
enum class EObjectProxyPlayerNode : uint8
{
    Root = 0,
    Session,
    Controller,
    Pawn,
    Profile,
    Inventory,
    Progression,
};

inline MString ResolveObjectPath(EObjectProxyPlayerNode Node)
{
    switch (Node)
    {
    case EObjectProxyPlayerNode::Root:
        return {};
    case EObjectProxyPlayerNode::Session:
        return "Session";
    case EObjectProxyPlayerNode::Controller:
        return "Controller";
    case EObjectProxyPlayerNode::Pawn:
        return "Pawn";
    case EObjectProxyPlayerNode::Profile:
        return "Profile";
    case EObjectProxyPlayerNode::Inventory:
        return "Profile.Inventory";
    case EObjectProxyPlayerNode::Progression:
        return "Profile.Progression";
    }

    return {};
}

class FBoundPlayerProxy
{
public:
    FBoundPlayerProxy() = default;

    FBoundPlayerProxy(
        uint64 InPlayerId,
        MObject* InContextObject,
        MString InObjectPath = {},
        EServerType InTargetServerType = EServerType::World)
        : PlayerId(InPlayerId)
        , ContextObject(InContextObject)
        , ObjectPath(std::move(InObjectPath))
        , TargetServerType(InTargetServerType)
    {
    }

    uint64 GetPlayerId() const
    {
        return PlayerId;
    }

    const MString& GetObjectPath() const
    {
        return ObjectPath;
    }

    FObjectProxyTarget GetTarget() const
    {
        FObjectProxyTarget Target;
        Target.RootType = EObjectProxyRootType::Player;
        Target.RootId = PlayerId;
        Target.ObjectPath = ObjectPath;
        Target.TargetServerType = TargetServerType;
        return Target;
    }

    FBoundPlayerProxy Root() const
    {
        return Node(EObjectProxyPlayerNode::Root);
    }

    FBoundPlayerProxy WithPath(MString InObjectPath) const
    {
        return FBoundPlayerProxy(PlayerId, ContextObject, std::move(InObjectPath), TargetServerType);
    }

    FBoundPlayerProxy Node(EObjectProxyPlayerNode InNode) const
    {
        return WithPath(ResolveObjectPath(InNode));
    }

    FBoundPlayerProxy Child(const char* Segment) const
    {
        if (!Segment || Segment[0] == '\0')
        {
            return *this;
        }

        MString NextPath = ObjectPath;
        if (!NextPath.empty())
        {
            NextPath += ".";
        }
        NextPath += Segment;
        return WithPath(std::move(NextPath));
    }

    FBoundPlayerProxy Session() const
    {
        return Node(EObjectProxyPlayerNode::Session);
    }

    FBoundPlayerProxy Controller() const
    {
        return Node(EObjectProxyPlayerNode::Controller);
    }

    FBoundPlayerProxy Pawn() const
    {
        return Node(EObjectProxyPlayerNode::Pawn);
    }

    FBoundPlayerProxy Profile() const
    {
        return Node(EObjectProxyPlayerNode::Profile);
    }

    FBoundPlayerProxy Inventory() const
    {
        return Node(EObjectProxyPlayerNode::Inventory);
    }

    FBoundPlayerProxy Progression() const
    {
        return Node(EObjectProxyPlayerNode::Progression);
    }

    template<typename TResponse, typename TRequest>
    MFuture<TResult<TResponse, FAppError>> Call(
        const char* FunctionName,
        const TRequest& Request) const
    {
        return MObjectProxyCall::Call<TResponse>(GetTarget(), FunctionName, Request, ContextObject);
    }

private:
    uint64 PlayerId = 0;
    MObject* ContextObject = nullptr;
    MString ObjectPath;
    EServerType TargetServerType = EServerType::World;
};

inline FBoundPlayerProxy Bind(
    uint64 PlayerId,
    MObject* ContextObject,
    EServerType TargetServerType = EServerType::World)
{
    return FBoundPlayerProxy(PlayerId, ContextObject, {}, TargetServerType);
}

inline FBoundPlayerProxy Bind(
    uint64 PlayerId,
    MObject* ContextObject,
    EObjectProxyPlayerNode Node,
    EServerType TargetServerType = EServerType::World)
{
    return Bind(PlayerId, ContextObject, TargetServerType).Node(Node);
}
} // namespace MPlayerProxyCall
