#include "Common/Net/ServerRpcRuntime.h"

#include "Common/Runtime/Json.h"
#include "Common/Runtime/Log/Logger.h"

#include <mutex>

namespace
{
struct SGeneratedRpcUnsupportedKey
{
    EServerType ServerType = EServerType::Unknown;
    MString FunctionName;

    bool operator<(const SGeneratedRpcUnsupportedKey& Other) const
    {
        if (ServerType != Other.ServerType)
        {
            return static_cast<uint8>(ServerType) < static_cast<uint8>(Other.ServerType);
        }

        return FunctionName < Other.FunctionName;
    }
};

std::mutex GGeneratedRpcUnsupportedMutex;
TMap<SGeneratedRpcUnsupportedKey, uint64> GGeneratedRpcUnsupportedCounts;

struct SGeneratedClientEntry
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

const char* ResolveClientDownlinkFunctionName(uint16 FunctionId)
{
    if (FunctionId == MClientDownlink::Id_OnLoginResponse())
    {
        return MClientDownlink::OnLoginResponse;
    }
    if (FunctionId == MClientDownlink::Id_OnActorCreate())
    {
        return MClientDownlink::OnActorCreate;
    }
    if (FunctionId == MClientDownlink::Id_OnActorUpdate())
    {
        return MClientDownlink::OnActorUpdate;
    }
    if (FunctionId == MClientDownlink::Id_OnActorDestroy())
    {
        return MClientDownlink::OnActorDestroy;
    }
    if (FunctionId == MClientDownlink::Id_OnInventoryPull())
    {
        return MClientDownlink::OnInventoryPull;
    }

    return nullptr;
}

SGeneratedClientRouteRequest::ERouteKind ParseGeneratedClientRouteKind(const char* RouteName)
{
    if (!RouteName || RouteName[0] == '\0')
    {
        return SGeneratedClientRouteRequest::ERouteKind::None;
    }

    const MString Route(RouteName);
    if (Route == "Login")
    {
        return SGeneratedClientRouteRequest::ERouteKind::Login;
    }
    if (Route == "World")
    {
        return SGeneratedClientRouteRequest::ERouteKind::World;
    }
    if (Route == "RouterResolved")
    {
        return SGeneratedClientRouteRequest::ERouteKind::RouterResolved;
    }

    return SGeneratedClientRouteRequest::ERouteKind::None;
}

EClientMessageType ParseGeneratedClientMessageType(const char* MessageName)
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

EServerType ParseGeneratedClientTargetServerType(const char* TargetName)
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

bool FindGeneratedClientEntryByMessage(
    const MClass* TargetClass,
    EClientMessageType MessageType,
    SGeneratedClientEntry& OutEntry)
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

bool FindGeneratedClientEntryByFunctionId(
    const MClass* TargetClass,
    uint16 FunctionId,
    SGeneratedClientEntry& OutEntry)
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

SGeneratedClientDispatchOutcome DispatchGeneratedClientEntry(
    MObject* TargetInstance,
    uint64 ConnectionId,
    const SGeneratedClientEntry& Entry,
    EClientMessageType MessageType,
    const TByteArray& Payload)
{
    SGeneratedClientDispatchOutcome Outcome;
    if (!TargetInstance || !Entry.Function)
    {
        return Outcome;
    }

    Outcome.OwnerType = Entry.OwnerClass ? Entry.OwnerClass->GetName().c_str() : nullptr;
    Outcome.FunctionName = Entry.Function->Name.c_str();

    MClass* TargetClass = TargetInstance->GetClass();
    if (!TargetClass)
    {
        return Outcome;
    }

    if (!Entry.Function->RouteName.empty())
    {
        auto* RouteTarget = dynamic_cast<IGeneratedClientRouteTarget*>(TargetInstance);
        if (!RouteTarget)
        {
            LOG_WARN("Generated client route target unsupported: class=%s function=%s route=%s",
                     TargetClass->GetName().c_str(),
                     Entry.Function->Name.c_str(),
                     Entry.Function->RouteName.c_str());
            Outcome.Result = EGeneratedClientDispatchResult::RouteTargetUnsupported;
            return Outcome;
        }

        SGeneratedClientRouteRequest Request;
        Request.ConnectionId = ConnectionId;
        Request.MessageType = ParseGeneratedClientMessageType(Entry.Function->MessageName.c_str());
        Request.FunctionName = Entry.Function->Name.c_str();
        Request.RouteKind = ParseGeneratedClientRouteKind(Entry.Function->RouteName.c_str());
        Request.RouteName = Entry.Function->RouteName.c_str();
        Request.TargetServerType = ParseGeneratedClientTargetServerType(Entry.Function->TargetName.c_str());
        Request.TargetName = Entry.Function->TargetName.c_str();
        Request.AuthMode = Entry.Function->AuthMode.c_str();
        Request.WrapMode = Entry.Function->WrapMode.c_str();
        Request.Payload = &Payload;
        Outcome.Result = RouteTarget->HandleGeneratedClientRoute(Request);
        if (Outcome.Result != EGeneratedClientDispatchResult::Routed)
        {
            LOG_WARN("Generated client route dispatch failed: class=%s function=%s route=%s result=%u",
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
        LOG_WARN("Generated client manifest entry missing function: class=%s function=%s",
                 TargetClass->GetName().c_str(),
                 Entry.Function->Name.c_str());
        Outcome.Result = EGeneratedClientDispatchResult::MissingFunction;
        return Outcome;
    }

    if (!Entry.Function->ClientParamBinder)
    {
        LOG_WARN("Generated client manifest entry missing binder: class=%s function=%s",
                 TargetClass->GetName().c_str(),
                 Entry.Function->Name.c_str());
        Outcome.Result = EGeneratedClientDispatchResult::MissingBinder;
        return Outcome;
    }

    TByteArray ParamStorage;
    if (!Entry.Function->ClientParamBinder(ConnectionId, Payload, ParamStorage))
    {
        LOG_WARN("Generated client dispatch param binding failed: class=%s function=%s message=%s",
                 TargetClass->GetName().c_str(),
                 Entry.Function->Name.c_str(),
                 GetClientMessageTypeName(MessageType));
        Outcome.Result = EGeneratedClientDispatchResult::ParamBindingFailed;
        return Outcome;
    }

    if (!TargetInstance->ProcessEvent(Func, ParamStorage.empty() ? nullptr : ParamStorage.data()))
    {
        LOG_WARN("Generated client dispatch failed: class=%s function=%s message=%s",
                 TargetClass->GetName().c_str(),
                 Entry.Function->Name.c_str(),
                 GetClientMessageTypeName(MessageType));
        Outcome.Result = EGeneratedClientDispatchResult::InvokeFailed;
        return Outcome;
    }

    Outcome.Result = EGeneratedClientDispatchResult::Handled;
    return Outcome;
}
}

const char* GetServerTypeDisplayName(EServerType ServerType)
{
    switch (ServerType)
    {
    case EServerType::Gateway:
        return "Gateway";
    case EServerType::Login:
        return "Login";
    case EServerType::World:
        return "World";
    case EServerType::Scene:
        return "Scene";
    case EServerType::Router:
        return "Router";
    case EServerType::Mgo:
        return "Mgo";
    default:
        return "Unknown";
    }
}

bool BuildServerRpcPayload(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutData)
{
    const uint32 PayloadSize = static_cast<uint32>(InPayload.size());

    OutData.clear();
    OutData.reserve(sizeof(FunctionId) + sizeof(PayloadSize) + PayloadSize);

    const uint8* FuncPtr = reinterpret_cast<const uint8*>(&FunctionId);
    OutData.insert(OutData.end(), FuncPtr, FuncPtr + sizeof(FunctionId));

    const uint8* SizePtr = reinterpret_cast<const uint8*>(&PayloadSize);
    OutData.insert(OutData.end(), SizePtr, SizePtr + sizeof(PayloadSize));

    if (PayloadSize > 0)
    {
        OutData.insert(OutData.end(), InPayload.begin(), InPayload.end());
    }

    return true;
}

bool BuildServerRpcMessage(const TByteArray& RpcPayload, TByteArray& OutPacket)
{
    OutPacket.clear();
    OutPacket.reserve(1 + RpcPayload.size());
    OutPacket.push_back(static_cast<uint8>(EServerMessageType::MT_RPC));
    OutPacket.insert(OutPacket.end(), RpcPayload.begin(), RpcPayload.end());
    return true;
}

bool SendServerRpcMessage(MServerConnection& Connection, const TByteArray& RpcPayload)
{
    return Connection.Send(
        static_cast<uint8>(EServerMessageType::MT_RPC),
        RpcPayload.empty() ? nullptr : RpcPayload.data(),
        static_cast<uint32>(RpcPayload.size()));
}

bool SendServerRpcMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& RpcPayload)
{
    if (!Connection)
    {
        return false;
    }

    return SendServerRpcMessage(*Connection, RpcPayload);
}

bool SendServerRpcMessage(INetConnection& Connection, const TByteArray& RpcPayload)
{
    TByteArray Packet;
    BuildServerRpcMessage(RpcPayload, Packet);
    return Connection.Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

bool SendServerRpcMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& RpcPayload)
{
    if (!Connection)
    {
        return false;
    }

    return SendServerRpcMessage(*Connection, RpcPayload);
}

bool BuildRpcPayloadForEndpoint(const SRpcEndpointBinding& Binding, const TByteArray& InPayload, TByteArray& OutData)
{
    if (!Binding.ClassName || !Binding.FunctionName)
    {
        return false;
    }

    const uint16 FunctionId = MGET_STABLE_RPC_FUNCTION_ID(Binding.ClassName, Binding.FunctionName);
    if (FunctionId == 0)
    {
        return false;
    }

    return BuildServerRpcPayload(FunctionId, InPayload, OutData);
}

bool FindGeneratedRpcEndpoint(EServerType ServerType, const char* FunctionName, SRpcEndpointBinding& OutBinding)
{
    if (!FunctionName)
    {
        return false;
    }

    const TVector<MClass*> Classes = MObject::GetAllClasses();
    for (MClass* Class : Classes)
    {
        if (!Class)
        {
            continue;
        }

        for (MFunction* Function : Class->GetFunctions())
        {
            if (!Function)
            {
                continue;
            }
            if (Function->EndpointServerType != ServerType)
            {
                continue;
            }
            if (Function->Name != FunctionName)
            {
                continue;
            }

            OutBinding.ServerType = ServerType;
            OutBinding.ClassName = Class->GetName().c_str();
            OutBinding.FunctionName = Function->Name.c_str();
            return true;
        }
    }

    return false;
}

bool ServerSupportsGeneratedRpc(EServerType ServerType, const char* FunctionName)
{
    SRpcEndpointBinding Binding;
    return FindGeneratedRpcEndpoint(ServerType, FunctionName, Binding);
}

size_t GetGeneratedRpcEntryCount()
{
    size_t Count = 0;
    for (MClass* Class : MObject::GetAllClasses())
    {
        if (!Class)
        {
            continue;
        }
        for (MFunction* Function : Class->GetFunctions())
        {
            if (Function && Function->EndpointServerType != EServerType::Unknown)
            {
                ++Count;
            }
        }
    }
    return Count;
}

TVector<MString> GetGeneratedRpcFunctionNames(EServerType ServerType)
{
    TVector<MString> Result;
    for (MClass* Class : MObject::GetAllClasses())
    {
        if (!Class)
        {
            continue;
        }
        for (MFunction* Function : Class->GetFunctions())
        {
            if (Function && Function->EndpointServerType == ServerType)
            {
                Result.push_back(Function->Name);
            }
        }
    }
    return Result;
}

MString BuildGeneratedRpcManifestJson(EServerType ServerType)
{
    MJsonWriter W = MJsonWriter::Object();
    W.Key("serverType");
    W.Value(GetServerTypeDisplayName(ServerType));
    W.Key("count");
    W.Value(static_cast<uint64>(GetGeneratedRpcFunctionNames(ServerType).size()));
    W.Key("functions");
    W.BeginArray();
    for (MClass* Class : MObject::GetAllClasses())
    {
        if (!Class)
        {
            continue;
        }
        for (MFunction* Function : Class->GetFunctions())
        {
            if (!Function || Function->EndpointServerType != ServerType)
            {
                continue;
            }
            W.BeginObject();
            W.Key("class");
            W.Value(Class->GetName());
            W.Key("function");
            W.Value(Function->Name);
            W.EndObject();
        }
    }
    W.EndArray();
    W.Key("functionsFlat");
    W.BeginArray();
    for (const MString& Name : GetGeneratedRpcFunctionNames(ServerType))
    {
        W.Value(Name);
    }
    W.EndArray();
    return W.ToString();
}

void ReportUnsupportedGeneratedRpcEndpoint(EServerType ServerType, const char* FunctionName)
{
    const MString Name = FunctionName ? MString(FunctionName) : MString();
    if (Name.empty())
    {
        return;
    }

    uint64 Count = 0;
    {
        std::lock_guard<std::mutex> Lock(GGeneratedRpcUnsupportedMutex);
        Count = ++GGeneratedRpcUnsupportedCounts[{ServerType, Name}];
    }

    if (Count == 1 || Count == 10 || Count == 100)
    {
        LOG_WARN("Generated RPC endpoint unsupported: target=%s function=%s count=%llu",
                 GetServerTypeDisplayName(ServerType),
                 Name.c_str(),
                 static_cast<unsigned long long>(Count));
    }
}

TVector<SGeneratedRpcUnsupportedStat> GetGeneratedRpcUnsupportedStats()
{
    TVector<SGeneratedRpcUnsupportedStat> Result;
    std::lock_guard<std::mutex> Lock(GGeneratedRpcUnsupportedMutex);
    Result.reserve(GGeneratedRpcUnsupportedCounts.size());
    for (const auto& [Key, Count] : GGeneratedRpcUnsupportedCounts)
    {
        Result.push_back(SGeneratedRpcUnsupportedStat{Key.ServerType, Key.FunctionName, Count});
    }
    return Result;
}

TVector<SGeneratedRpcUnsupportedStat> GetGeneratedRpcUnsupportedStats(EServerType ServerType)
{
    TVector<SGeneratedRpcUnsupportedStat> Result;
    std::lock_guard<std::mutex> Lock(GGeneratedRpcUnsupportedMutex);
    for (const auto& [Key, Count] : GGeneratedRpcUnsupportedCounts)
    {
        if (Key.ServerType == ServerType)
        {
            Result.push_back(SGeneratedRpcUnsupportedStat{Key.ServerType, Key.FunctionName, Count});
        }
    }
    return Result;
}

MString BuildGeneratedRpcUnsupportedStatsJson()
{
    MJsonWriter W = MJsonWriter::Array();
    for (const SGeneratedRpcUnsupportedStat& Stat : GetGeneratedRpcUnsupportedStats())
    {
        W.BeginObject();
        W.Key("serverType");
        W.Value(GetServerTypeDisplayName(Stat.ServerType));
        W.Key("function");
        W.Value(Stat.FunctionName);
        W.Key("count");
        W.Value(Stat.Count);
        W.EndObject();
    }
    return W.ToString();
}

MString BuildGeneratedRpcUnsupportedStatsJson(EServerType ServerType)
{
    MJsonWriter W = MJsonWriter::Array();
    for (const SGeneratedRpcUnsupportedStat& Stat : GetGeneratedRpcUnsupportedStats(ServerType))
    {
        W.BeginObject();
        W.Key("serverType");
        W.Value(GetServerTypeDisplayName(Stat.ServerType));
        W.Key("function");
        W.Value(Stat.FunctionName);
        W.Key("count");
        W.Value(Stat.Count);
        W.EndObject();
    }
    return W.ToString();
}

bool TryInvokeServerRpc(MObject* ServiceInstance, const TByteArray& Data, ERpcType ExpectedType)
{
    if (!ServiceInstance)
    {
        return false;
    }

    if (Data.size() < sizeof(uint16) + sizeof(uint32))
    {
        LOG_WARN("TryInvokeServerRpc: packet too small (size=%llu)",
                 static_cast<unsigned long long>(Data.size()));
        return false;
    }

    size_t Offset = 0;
    uint16 FunctionId = 0;
    uint32 PayloadSize = 0;

    std::memcpy(&FunctionId, Data.data() + Offset, sizeof(FunctionId));
    Offset += sizeof(FunctionId);

    std::memcpy(&PayloadSize, Data.data() + Offset, sizeof(PayloadSize));
    Offset += sizeof(PayloadSize);

    if (Offset + PayloadSize > Data.size())
    {
        LOG_WARN("TryInvokeServerRpc: payload out of range (size=%llu, payload=%u)",
                 static_cast<unsigned long long>(Data.size()),
                 static_cast<unsigned>(PayloadSize));
        return false;
    }

    MClass* ServiceClass = ServiceInstance->GetClass();
    if (!ServiceClass)
    {
        LOG_ERROR("TryInvokeServerRpc: ServiceInstance has no class");
        return false;
    }

    MFunction* FuncMeta = ServiceClass->FindFunctionById(FunctionId);
    if (!FuncMeta)
    {
        LOG_WARN("TryInvokeServerRpc: unknown FunctionId=%u",
                 static_cast<unsigned>(FunctionId));
        return false;
    }

    if (FuncMeta->RpcType != ExpectedType)
    {
        LOG_WARN("TryInvokeServerRpc: RpcType mismatch for FunctionId=%u (expected=%d, actual=%d)",
                 static_cast<unsigned>(FunctionId),
                 static_cast<int>(ExpectedType),
                 static_cast<int>(FuncMeta->RpcType));
        return false;
    }

    TByteArray Payload;
    if (PayloadSize > 0)
    {
        Payload.resize(PayloadSize);
        std::memcpy(Payload.data(), Data.data() + Offset, PayloadSize);
    }

    MReflectArchive Ar(Payload);
    return ServiceInstance->InvokeSerializedFunction(FuncMeta, Ar);
}

bool TryDispatchGeneratedClientMessage(
    MObject* TargetInstance,
    uint64 ConnectionId,
    EClientMessageType MessageType,
    const TByteArray& Payload)
{
    return DispatchGeneratedClientMessage(TargetInstance, ConnectionId, MessageType, Payload).Result !=
           EGeneratedClientDispatchResult::NotFound;
}

SGeneratedClientDispatchOutcome DispatchGeneratedClientMessage(
    MObject* TargetInstance,
    uint64 ConnectionId,
    EClientMessageType MessageType,
    const TByteArray& Payload)
{
    SGeneratedClientDispatchOutcome Outcome;
    if (!TargetInstance)
    {
        return Outcome;
    }

    SGeneratedClientEntry Entry;
    if (!FindGeneratedClientEntryByMessage(TargetInstance->GetClass(), MessageType, Entry))
    {
        return Outcome;
    }

    return DispatchGeneratedClientEntry(TargetInstance, ConnectionId, Entry, MessageType, Payload);
}

SGeneratedClientDispatchOutcome DispatchGeneratedClientFunction(
    MObject* TargetInstance,
    uint64 ConnectionId,
    uint16 FunctionId,
    const TByteArray& Payload)
{
    SGeneratedClientDispatchOutcome Outcome;
    if (!TargetInstance)
    {
        return Outcome;
    }

    SGeneratedClientEntry Entry;
    if (!FindGeneratedClientEntryByFunctionId(TargetInstance->GetClass(), FunctionId, Entry))
    {
        return Outcome;
    }

    return DispatchGeneratedClientEntry(
        TargetInstance,
        ConnectionId,
        Entry,
        EClientMessageType::MT_FunctionCall,
        Payload);
}

uint16 GetClientDownlinkFunctionId(const char* FunctionName)
{
    if (!FunctionName || FunctionName[0] == '\0')
    {
        return 0;
    }

    return ComputeStableReflectId(MClientDownlink::ScopeName, FunctionName);
}

const char* GetClientDownlinkFunctionName(uint16 FunctionId)
{
    return ResolveClientDownlinkFunctionName(FunctionId);
}

bool BuildClientFunctionCallPacket(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutPacket)
{
    if (FunctionId == 0)
    {
        return false;
    }

    OutPacket.clear();
    OutPacket.reserve(1 + sizeof(FunctionId) + sizeof(uint32) + InPayload.size());
    OutPacket.push_back(static_cast<uint8>(EClientMessageType::MT_FunctionCall));
    AppendValue(OutPacket, FunctionId);
    AppendValue(OutPacket, static_cast<uint32>(InPayload.size()));
    OutPacket.insert(OutPacket.end(), InPayload.begin(), InPayload.end());
    return true;
}
