#include "Common/Net/Rpc/RpcClientCall.h"

#include "Common/Runtime/Log/Logger.h"

extern thread_local uint64 GCurrentClientConnectionId;
extern thread_local SClientCallContext GCurrentClientCallContext;

namespace
{
class FScopedClientConnectionContext
{
public:
    explicit FScopedClientConnectionContext(uint64 InConnectionId)
        : PreviousConnectionId(GCurrentClientConnectionId)
    {
        GCurrentClientConnectionId = InConnectionId;
    }

    ~FScopedClientConnectionContext()
    {
        GCurrentClientConnectionId = PreviousConnectionId;
    }

private:
    uint64 PreviousConnectionId = 0;
};

class FScopedClientCallContext
{
public:
    explicit FScopedClientCallContext(const SClientCallContext& InContext)
        : PreviousContext(GCurrentClientCallContext)
    {
        GCurrentClientCallContext = InContext;
    }

    ~FScopedClientCallContext()
    {
        GCurrentClientCallContext = PreviousContext;
    }

private:
    SClientCallContext PreviousContext;
};

struct SClientEntry
{
    MClass* OwnerClass = nullptr;
    MFunction* Function = nullptr;
};

template<typename TFunc>
void ForEachClassFunction(MClass* InClass, TFunc&& Func)
{
    for (MClass* ClassIt = InClass; ClassIt; ClassIt = const_cast<MClass*>(ClassIt->GetParentClass()))
    {
        for (MFunction* Function : ClassIt->GetFunctions())
        {
            if (Function)
            {
                Func(ClassIt, Function);
            }
        }
    }
}

const char* GetClientMessageTypeName(EClientMessageType MessageType)
{
    switch (MessageType)
    {
    case EClientMessageType::MT_Login:
        return "MT_Login";
    case EClientMessageType::MT_Handshake:
        return "MT_Handshake";
    case EClientMessageType::MT_PlayerMove:
        return "MT_PlayerMove";
    case EClientMessageType::MT_RPC:
        return "MT_RPC";
    case EClientMessageType::MT_Chat:
        return "MT_Chat";
    case EClientMessageType::MT_Heartbeat:
        return "MT_Heartbeat";
    case EClientMessageType::MT_Error:
        return "MT_Error";
    case EClientMessageType::MT_FunctionCall:
        return "MT_FunctionCall";
    default:
        return "Unknown";
    }
}

SClientRouteRequest::ERouteKind ParseClientRouteKind(const char* RouteName)
{
    if (!RouteName || RouteName[0] == '\0')
    {
        return SClientRouteRequest::ERouteKind::None;
    }

    const MString Route(RouteName);
    if (Route == "Login")
    {
        return SClientRouteRequest::ERouteKind::Login;
    }
    if (Route == "World")
    {
        return SClientRouteRequest::ERouteKind::World;
    }
    if (Route == "RouterResolved")
    {
        return SClientRouteRequest::ERouteKind::RouterResolved;
    }

    return SClientRouteRequest::ERouteKind::None;
}

EClientMessageType ParseClientMessageType(const char* MessageName)
{
    if (!MessageName || MessageName[0] == '\0')
    {
        return EClientMessageType::MT_Error;
    }

    const MString Message(MessageName);
    if (Message == "MT_Login")
    {
        return EClientMessageType::MT_Login;
    }
    if (Message == "MT_Handshake")
    {
        return EClientMessageType::MT_Handshake;
    }
    if (Message == "MT_PlayerMove")
    {
        return EClientMessageType::MT_PlayerMove;
    }
    if (Message == "MT_RPC")
    {
        return EClientMessageType::MT_RPC;
    }
    if (Message == "MT_Chat")
    {
        return EClientMessageType::MT_Chat;
    }
    if (Message == "MT_Heartbeat")
    {
        return EClientMessageType::MT_Heartbeat;
    }
    if (Message == "MT_FunctionCall")
    {
        return EClientMessageType::MT_FunctionCall;
    }
    return EClientMessageType::MT_Error;
}

EServerType ParseClientTargetServerType(const char* TargetName)
{
    if (!TargetName || TargetName[0] == '\0')
    {
        return EServerType::Unknown;
    }

    const MString Target(TargetName);
    if (Target == "Login")
    {
        return EServerType::Login;
    }
    if (Target == "World")
    {
        return EServerType::World;
    }
    if (Target == "Scene")
    {
        return EServerType::Scene;
    }
    if (Target == "Router")
    {
        return EServerType::Router;
    }
    if (Target == "Gateway")
    {
        return EServerType::Gateway;
    }
    if (Target == "Mgo")
    {
        return EServerType::Mgo;
    }

    return EServerType::Unknown;
}

bool FindClientEntryByMessage(
    const MClass* TargetClass,
    EClientMessageType MessageType,
    SClientEntry& OutEntry)
{
    if (!TargetClass)
    {
        return false;
    }

    const char* MessageName = GetClientMessageTypeName(MessageType);
    if (!MessageName || MessageName[0] == '\0')
    {
        return false;
    }

    bool bFound = false;
    ForEachClassFunction(const_cast<MClass*>(TargetClass), [&](MClass* OwnerClass, MFunction* Function)
    {
        if (bFound)
        {
            return;
        }
        if (!Function->MessageName.empty() &&
            Function->MessageName == MessageName &&
            (!Function->Transport.empty() || Function->ClientParamBinder != nullptr))
        {
            OutEntry.OwnerClass = OwnerClass;
            OutEntry.Function = Function;
            bFound = true;
        }
    });
    return bFound;
}

bool FindClientEntryByFunctionId(
    const MClass* TargetClass,
    uint16 FunctionId,
    SClientEntry& OutEntry)
{
    if (!TargetClass || FunctionId == 0)
    {
        return false;
    }

    MFunction* Function = const_cast<MClass*>(TargetClass)->FindFunctionById(FunctionId);
    if (!Function)
    {
        return false;
    }

    if (Function->Transport.empty() && Function->ClientParamBinder == nullptr)
    {
        return false;
    }

    OutEntry.OwnerClass = const_cast<MClass*>(TargetClass);
    OutEntry.Function = Function;
    return true;
}

SClientDispatchOutcome DispatchClientEntry(
    MObject* TargetInstance,
    uint64 ConnectionId,
    const SClientEntry& Entry,
    uint64 CallId,
    EClientMessageType MessageType,
    const TByteArray& Payload,
    const TSharedPtr<IClientResponseTarget>& OverrideResponseTarget = nullptr)
{
    SClientDispatchOutcome Outcome;
    if (!TargetInstance || !Entry.Function)
    {
        return Outcome;
    }

    FScopedClientConnectionContext Scope(ConnectionId);

    Outcome.OwnerType = Entry.OwnerClass ? Entry.OwnerClass->GetName().c_str() : nullptr;
    Outcome.FunctionName = Entry.Function->Name.c_str();

    MClass* TargetClass = TargetInstance->GetClass();
    if (!TargetClass)
    {
        return Outcome;
    }

    if (!Entry.Function->RouteName.empty())
    {
        auto* RouteTarget = TargetInstance->GetClientRouteTarget();
        if (!RouteTarget)
        {
            LOG_WARN("Client route target unsupported: class=%s function=%s route=%s",
                     TargetClass->GetName().c_str(),
                     Entry.Function->Name.c_str(),
                     Entry.Function->RouteName.c_str());
            Outcome.Result = EClientDispatchResult::RouteTargetUnsupported;
            return Outcome;
        }

        SClientRouteRequest Request;
        Request.ConnectionId = ConnectionId;
        Request.MessageType = ParseClientMessageType(Entry.Function->MessageName.c_str());
        Request.FunctionName = Entry.Function->Name.c_str();
        Request.RouteKind = ParseClientRouteKind(Entry.Function->RouteName.c_str());
        Request.RouteName = Entry.Function->RouteName.c_str();
        Request.TargetServerType = ParseClientTargetServerType(Entry.Function->TargetName.c_str());
        Request.TargetName = Entry.Function->TargetName.c_str();
        Request.AuthMode = Entry.Function->AuthMode.c_str();
        Request.WrapMode = Entry.Function->WrapMode.c_str();
        Request.Payload = &Payload;
        Outcome.Result = RouteTarget->HandleClientRoute(Request);
        if (Outcome.Result != EClientDispatchResult::Routed)
        {
            LOG_WARN("Client route dispatch failed: class=%s function=%s route=%s result=%u",
                     TargetClass->GetName().c_str(),
                     Entry.Function->Name.c_str(),
                     Entry.Function->RouteName.c_str(),
                     static_cast<unsigned>(Outcome.Result));
            return Outcome;
        }

        return Outcome;
    }

    MFunction* Func = Entry.Function;
    if (!Func)
    {
        LOG_WARN("Client manifest entry missing function: class=%s function=%s",
                 TargetClass->GetName().c_str(),
                 Entry.Function->Name.c_str());
        Outcome.Result = EClientDispatchResult::MissingFunction;
        return Outcome;
    }

    if (Entry.Function->ClientCallHandler)
    {
        TSharedPtr<IClientResponseTarget> ResponseTarget = OverrideResponseTarget;
        if (!ResponseTarget)
        {
            if (auto* LegacyTarget = TargetInstance->GetClientResponseTarget())
            {
                ResponseTarget = TSharedPtr<IClientResponseTarget>(LegacyTarget, [](IClientResponseTarget*) {});
            }
        }
        if (!ResponseTarget)
        {
            LOG_WARN("Client call response target unsupported: class=%s function=%s",
                     TargetClass->GetName().c_str(),
                     Entry.Function->Name.c_str());
            Outcome.Result = EClientDispatchResult::RouteTargetUnsupported;
            return Outcome;
        }

        const SClientCallContext ClientCallContext{
            ConnectionId,
            Entry.Function->FunctionId,
            CallId,
            ResponseTarget,
        };
        FScopedClientCallContext ClientCallScope(ClientCallContext);

        TByteArray ResponsePayload;
        const EGeneratedClientCallHandlerResult HandlerResult =
            Entry.Function->ClientCallHandler(TargetInstance, ConnectionId, Payload, ResponsePayload);
        if (HandlerResult == EGeneratedClientCallHandlerResult::Failed)
        {
            LOG_WARN("Client call invoke failed: class=%s function=%s",
                     TargetClass->GetName().c_str(),
                     Entry.Function->Name.c_str());
            Outcome.Result = EClientDispatchResult::InvokeFailed;
            return Outcome;
        }

        if (HandlerResult == EGeneratedClientCallHandlerResult::Deferred)
        {
            Outcome.Result = EClientDispatchResult::Handled;
            return Outcome;
        }

        if (!ResponseTarget->SendClientResponse(ConnectionId, Entry.Function->FunctionId, CallId, ResponsePayload))
        {
            LOG_WARN("Client call response send failed: class=%s function=%s connection=%llu",
                     TargetClass->GetName().c_str(),
                     Entry.Function->Name.c_str(),
                     static_cast<unsigned long long>(ConnectionId));
            Outcome.Result = EClientDispatchResult::InvokeFailed;
            return Outcome;
        }

        Outcome.Result = EClientDispatchResult::Handled;
        return Outcome;
    }

    if (!Entry.Function->ClientParamBinder)
    {
        LOG_WARN("Client manifest entry missing binder: class=%s function=%s",
                 TargetClass->GetName().c_str(),
                 Entry.Function->Name.c_str());
        Outcome.Result = EClientDispatchResult::MissingBinder;
        return Outcome;
    }

    TByteArray ParamStorage;
    if (!Entry.Function->ClientParamBinder(ConnectionId, Payload, ParamStorage))
    {
        LOG_WARN("Client dispatch param binding failed: class=%s function=%s message=%s",
                 TargetClass->GetName().c_str(),
                 Entry.Function->Name.c_str(),
                 GetClientMessageTypeName(MessageType));
        Outcome.Result = EClientDispatchResult::ParamBindingFailed;
        return Outcome;
    }

    if (!TargetInstance->ProcessEvent(Func, ParamStorage.empty() ? nullptr : ParamStorage.data()))
    {
        LOG_WARN("Client dispatch failed: class=%s function=%s message=%s",
                 TargetClass->GetName().c_str(),
                 Entry.Function->Name.c_str(),
                 GetClientMessageTypeName(MessageType));
        Outcome.Result = EClientDispatchResult::InvokeFailed;
        return Outcome;
    }

    Outcome.Result = EClientDispatchResult::Handled;
    return Outcome;
}
} // namespace

bool TryDispatchClientMessage(
    MObject* TargetInstance,
    uint64 ConnectionId,
    EClientMessageType MessageType,
    const TByteArray& Payload)
{
    return DispatchClientMessage(TargetInstance, ConnectionId, MessageType, Payload).Result !=
           EClientDispatchResult::NotFound;
}

SClientDispatchOutcome DispatchClientMessage(
    MObject* TargetInstance,
    uint64 ConnectionId,
    EClientMessageType MessageType,
    const TByteArray& Payload)
{
    SClientDispatchOutcome Outcome;
    if (!TargetInstance)
    {
        return Outcome;
    }

    SClientEntry Entry;
    if (!FindClientEntryByMessage(TargetInstance->GetClass(), MessageType, Entry))
    {
        return Outcome;
    }

    return DispatchClientEntry(TargetInstance, ConnectionId, Entry, 0, MessageType, Payload);
}

SClientDispatchOutcome DispatchClientFunction(
    MObject* TargetInstance,
    uint64 ConnectionId,
    uint16 FunctionId,
    uint64 CallId,
    const TByteArray& Payload)
{
    SClientDispatchOutcome Outcome;
    if (!TargetInstance)
    {
        return Outcome;
    }

    SClientEntry Entry;
    if (!FindClientEntryByFunctionId(TargetInstance->GetClass(), FunctionId, Entry))
    {
        return Outcome;
    }

    return DispatchClientEntry(
        TargetInstance,
        ConnectionId,
        Entry,
        CallId,
        EClientMessageType::MT_FunctionCall,
        Payload);
}

SClientDispatchOutcome DispatchClientFunction(
    MObject* TargetInstance,
    uint64 ConnectionId,
    uint16 FunctionId,
    uint64 CallId,
    const TByteArray& Payload,
    const TSharedPtr<IClientResponseTarget>& ResponseTarget)
{
    SClientDispatchOutcome Outcome;
    if (!TargetInstance)
    {
        return Outcome;
    }

    SClientEntry Entry;
    if (!FindClientEntryByFunctionId(TargetInstance->GetClass(), FunctionId, Entry))
    {
        return Outcome;
    }

    return DispatchClientEntry(
        TargetInstance,
        ConnectionId,
        Entry,
        CallId,
        EClientMessageType::MT_FunctionCall,
        Payload,
        ResponseTarget);
}

const MFunction* FindClientFunctionById(const MClass* TargetClass, uint16 FunctionId)
{
    SClientEntry Entry;
    if (!FindClientEntryByFunctionId(TargetClass, FunctionId, Entry))
    {
        return nullptr;
    }

    return Entry.Function;
}
