#include "Common/ServerRpcRuntime.h"

#include "Build/Generated/MClientManifest.mgenerated.h"
#include "Build/Generated/MRpcManifest.mgenerated.h"
#include "Core/Json.h"
#include "Common/Logger.h"

#include <mutex>

namespace
{
struct SGeneratedRpcUnsupportedKey
{
    EServerType ServerType = EServerType::Unknown;
    FString FunctionName;

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

    const FString Route(RouteName);
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

    const FString Message(MessageName);
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

    const FString Target(TargetName);
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

const MClientManifest::SEntry* FindGeneratedClientEntryByMessage(
    const MClass* TargetClass,
    EClientMessageType MessageType)
{
    if (!TargetClass)
    {
        return nullptr;
    }

    const char* MessageName = GetClientMessageTypeName(MessageType);
    return MClientManifest::FindByMessageName(TargetClass->GetName().c_str(), MessageName);
}

const MClientManifest::SEntry* FindGeneratedClientEntryByFunctionId(
    const MClass* TargetClass,
    uint16 FunctionId)
{
    if (!TargetClass || FunctionId == 0)
    {
        return nullptr;
    }

    const MClientManifest::SEntry* Entry = MClientManifest::FindByFunctionId(FunctionId);
    if (!Entry || !Entry->OwnerType || TargetClass->GetName() != Entry->OwnerType)
    {
        return nullptr;
    }
    return Entry;
}

SGeneratedClientDispatchOutcome DispatchGeneratedClientEntry(
    MReflectObject* TargetInstance,
    uint64 ConnectionId,
    const MClientManifest::SEntry* Entry,
    EClientMessageType MessageType,
    const TArray& Payload)
{
    SGeneratedClientDispatchOutcome Outcome;
    if (!TargetInstance || !Entry)
    {
        return Outcome;
    }

    Outcome.OwnerType = Entry->OwnerType;
    Outcome.FunctionName = Entry->FunctionName;

    MClass* TargetClass = TargetInstance->GetClass();
    if (!TargetClass)
    {
        return Outcome;
    }

    if (Entry->RouteName && Entry->RouteName[0] != '\0')
    {
        auto* RouteTarget = dynamic_cast<IGeneratedClientRouteTarget*>(TargetInstance);
        if (!RouteTarget)
        {
            LOG_WARN("Generated client route target unsupported: class=%s function=%s route=%s",
                     TargetClass->GetName().c_str(),
                     Entry->FunctionName,
                     Entry->RouteName);
            Outcome.Result = EGeneratedClientDispatchResult::RouteTargetUnsupported;
            return Outcome;
        }

        SGeneratedClientRouteRequest Request;
        Request.ConnectionId = ConnectionId;
        Request.MessageType = ParseGeneratedClientMessageType(Entry->MessageName);
        Request.FunctionName = Entry->FunctionName;
        Request.RouteKind = ParseGeneratedClientRouteKind(Entry->RouteName);
        Request.RouteName = Entry->RouteName;
        Request.TargetServerType = ParseGeneratedClientTargetServerType(Entry->TargetName);
        Request.TargetName = Entry->TargetName;
        Request.AuthMode = Entry->AuthMode;
        Request.WrapMode = Entry->WrapMode;
        Request.Payload = &Payload;
        Outcome.Result = RouteTarget->HandleGeneratedClientRoute(Request);
        if (Outcome.Result != EGeneratedClientDispatchResult::Routed)
        {
            LOG_WARN("Generated client route dispatch failed: class=%s function=%s route=%s result=%u",
                     TargetClass->GetName().c_str(),
                     Entry->FunctionName,
                     Entry->RouteName,
                     static_cast<unsigned>(Outcome.Result));
            return Outcome;
        }

        return Outcome;
    }

    MFunction* Func = TargetClass->FindFunction(Entry->FunctionName);
    if (!Func)
    {
        LOG_WARN("Generated client manifest entry missing function: class=%s function=%s",
                 TargetClass->GetName().c_str(),
                 Entry->FunctionName);
        Outcome.Result = EGeneratedClientDispatchResult::MissingFunction;
        return Outcome;
    }

    if (!Entry->BindParams)
    {
        LOG_WARN("Generated client manifest entry missing binder: class=%s function=%s",
                 TargetClass->GetName().c_str(),
                 Entry->FunctionName);
        Outcome.Result = EGeneratedClientDispatchResult::MissingBinder;
        return Outcome;
    }

    TArray ParamStorage;
    if (!Entry->BindParams(ConnectionId, Payload, ParamStorage))
    {
        LOG_WARN("Generated client dispatch param binding failed: class=%s function=%s message=%s",
                 TargetClass->GetName().c_str(),
                 Entry->FunctionName,
                 GetClientMessageTypeName(MessageType));
        Outcome.Result = EGeneratedClientDispatchResult::ParamBindingFailed;
        return Outcome;
    }

    if (!TargetInstance->ProcessEvent(Func, ParamStorage.empty() ? nullptr : ParamStorage.data()))
    {
        LOG_WARN("Generated client dispatch failed: class=%s function=%s message=%s",
                 TargetClass->GetName().c_str(),
                 Entry->FunctionName,
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

bool BuildServerRpcPayload(uint16 FunctionId, const TArray& InPayload, TArray& OutData)
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

bool BuildServerRpcMessage(const TArray& RpcPayload, TArray& OutPacket)
{
    OutPacket.clear();
    OutPacket.reserve(1 + RpcPayload.size());
    OutPacket.push_back(static_cast<uint8>(EServerMessageType::MT_RPC));
    OutPacket.insert(OutPacket.end(), RpcPayload.begin(), RpcPayload.end());
    return true;
}

bool SendServerRpcMessage(MServerConnection& Connection, const TArray& RpcPayload)
{
    return Connection.Send(
        static_cast<uint8>(EServerMessageType::MT_RPC),
        RpcPayload.empty() ? nullptr : RpcPayload.data(),
        static_cast<uint32>(RpcPayload.size()));
}

bool SendServerRpcMessage(const TSharedPtr<MServerConnection>& Connection, const TArray& RpcPayload)
{
    if (!Connection)
    {
        return false;
    }

    return SendServerRpcMessage(*Connection, RpcPayload);
}

bool SendServerRpcMessage(INetConnection& Connection, const TArray& RpcPayload)
{
    TArray Packet;
    BuildServerRpcMessage(RpcPayload, Packet);
    return Connection.Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

bool SendServerRpcMessage(const TSharedPtr<INetConnection>& Connection, const TArray& RpcPayload)
{
    if (!Connection)
    {
        return false;
    }

    return SendServerRpcMessage(*Connection, RpcPayload);
}

bool BuildRpcPayloadForEndpoint(const SRpcEndpointBinding& Binding, const TArray& InPayload, TArray& OutData)
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

    const MRpcManifest::SEntry* Entries = MRpcManifest::GetEntries();
    for (size_t Index = 0; Index < MRpcManifest::GetEntryCount(); ++Index)
    {
        const MRpcManifest::SEntry& Entry = Entries[Index];
        if (Entry.ServerType == ServerType && Entry.FunctionName && FString(Entry.FunctionName) == FunctionName)
        {
            OutBinding.ServerType = Entry.ServerType;
            OutBinding.ClassName = Entry.ClassName;
            OutBinding.FunctionName = Entry.FunctionName;
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

TVector<FString> GetGeneratedRpcFunctionNames(EServerType ServerType)
{
    TVector<FString> Result;
    MRpcManifest::ForEachSupportedFunction(
        ServerType,
        [&Result](const MRpcManifest::SEntry& Entry)
        {
            Result.push_back(Entry.FunctionName ? FString(Entry.FunctionName) : FString());
        });
    return Result;
}

FString BuildGeneratedRpcManifestJson(EServerType ServerType)
{
    MJsonWriter W = MJsonWriter::Object();
    W.Key("serverType");
    W.Value(GetServerTypeDisplayName(ServerType));
    W.Key("count");
    W.Value(static_cast<uint64>(MRpcManifest::GetSupportedFunctionCount(ServerType)));
    W.Key("functions");
    W.BeginArray();
    MRpcManifest::ForEachSupportedFunction(
        ServerType,
        [&W](const MRpcManifest::SEntry& Entry)
        {
            W.BeginObject();
            W.Key("class");
            W.Value(Entry.ClassName ? Entry.ClassName : "");
            W.Key("function");
            W.Value(Entry.FunctionName ? Entry.FunctionName : "");
            W.EndObject();
        });
    W.EndArray();
    W.Key("functionsFlat");
    W.BeginArray();
    for (const FString& Name : GetGeneratedRpcFunctionNames(ServerType))
    {
        W.Value(Name);
    }
    W.EndArray();
    return W.ToString();
}

void ReportUnsupportedGeneratedRpcEndpoint(EServerType ServerType, const char* FunctionName)
{
    const FString Name = FunctionName ? FString(FunctionName) : FString();
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

FString BuildGeneratedRpcUnsupportedStatsJson()
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

FString BuildGeneratedRpcUnsupportedStatsJson(EServerType ServerType)
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

bool TryInvokeServerRpc(MReflectObject* ServiceInstance, const TArray& Data, ERpcType ExpectedType)
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

    TArray Payload;
    if (PayloadSize > 0)
    {
        Payload.resize(PayloadSize);
        std::memcpy(Payload.data(), Data.data() + Offset, PayloadSize);
    }

    MReflectArchive Ar(Payload);
    return ServiceInstance->InvokeSerializedFunction(FuncMeta, Ar);
}

bool TryDispatchGeneratedClientMessage(
    MReflectObject* TargetInstance,
    uint64 ConnectionId,
    EClientMessageType MessageType,
    const TArray& Payload)
{
    return DispatchGeneratedClientMessage(TargetInstance, ConnectionId, MessageType, Payload).Result !=
           EGeneratedClientDispatchResult::NotFound;
}

SGeneratedClientDispatchOutcome DispatchGeneratedClientMessage(
    MReflectObject* TargetInstance,
    uint64 ConnectionId,
    EClientMessageType MessageType,
    const TArray& Payload)
{
    SGeneratedClientDispatchOutcome Outcome;
    if (!TargetInstance)
    {
        return Outcome;
    }

    const MClientManifest::SEntry* Entry = FindGeneratedClientEntryByMessage(TargetInstance->GetClass(), MessageType);
    if (!Entry)
    {
        return Outcome;
    }

    return DispatchGeneratedClientEntry(TargetInstance, ConnectionId, Entry, MessageType, Payload);
}

SGeneratedClientDispatchOutcome DispatchGeneratedClientFunction(
    MReflectObject* TargetInstance,
    uint64 ConnectionId,
    uint16 FunctionId,
    const TArray& Payload)
{
    SGeneratedClientDispatchOutcome Outcome;
    if (!TargetInstance)
    {
        return Outcome;
    }

    const MClientManifest::SEntry* Entry = FindGeneratedClientEntryByFunctionId(TargetInstance->GetClass(), FunctionId);
    if (!Entry)
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

bool BuildClientFunctionCallPacket(uint16 FunctionId, const TArray& InPayload, TArray& OutPacket)
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
