#include "Common/Net/Rpc/RpcManifest.h"

#include "Common/Runtime/Json.h"
#include "Common/Runtime/Log/Logger.h"

#include <mutex>

namespace
{
struct SRpcUnsupportedKey
{
    EServerType ServerType = EServerType::Unknown;
    MString FunctionName;

    bool operator<(const SRpcUnsupportedKey& Other) const
    {
        if (ServerType != Other.ServerType)
        {
            return static_cast<uint8>(ServerType) < static_cast<uint8>(Other.ServerType);
        }

        return FunctionName < Other.FunctionName;
    }
};

std::mutex GRpcUnsupportedMutex;
TMap<SRpcUnsupportedKey, uint64> GRpcUnsupportedCounts;
}

EServerType ParseServerTargetType(const char* TargetName)
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

const char* GetServerEndpointClassName(EServerType ServerType)
{
    switch (ServerType)
    {
    case EServerType::Gateway:
        return "MGatewayServer";
    case EServerType::Login:
        return "MLoginServer";
    case EServerType::World:
        return "MWorldServer";
    case EServerType::Scene:
        return "MSceneServer";
    case EServerType::Router:
        return "MRouterServer";
    case EServerType::Mgo:
        return "MMgoServer";
    default:
        return nullptr;
    }
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

bool FindRpcEndpoint(EServerType ServerType, const char* FunctionName, SRpcEndpointBinding& OutBinding)
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

bool ServerSupportsRpc(EServerType ServerType, const char* FunctionName)
{
    SRpcEndpointBinding Binding;
    return FindRpcEndpoint(ServerType, FunctionName, Binding);
}

size_t GetRpcEntryCount()
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

TVector<MString> GetRpcFunctionNames(EServerType ServerType)
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

MString BuildRpcManifestJson(EServerType ServerType)
{
    MJsonWriter W = MJsonWriter::Object();
    W.Key("serverType");
    W.Value(GetServerTypeDisplayName(ServerType));
    W.Key("count");
    W.Value(static_cast<uint64>(GetRpcFunctionNames(ServerType).size()));
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
    for (const MString& Name : GetRpcFunctionNames(ServerType))
    {
        W.Value(Name);
    }
    W.EndArray();
    return W.ToString();
}

void ReportUnsupportedRpcEndpoint(EServerType ServerType, const char* FunctionName)
{
    const MString Name = FunctionName ? MString(FunctionName) : MString();
    if (Name.empty())
    {
        return;
    }

    uint64 Count = 0;
    {
        std::lock_guard<std::mutex> Lock(GRpcUnsupportedMutex);
        Count = ++GRpcUnsupportedCounts[{ServerType, Name}];
    }

    if (Count == 1 || Count == 10 || Count == 100)
    {
        LOG_WARN("RPC endpoint unsupported: target=%s function=%s count=%llu",
                 GetServerTypeDisplayName(ServerType),
                 Name.c_str(),
                 static_cast<unsigned long long>(Count));
    }
}

TVector<SRpcUnsupportedStat> GetRpcUnsupportedStats()
{
    TVector<SRpcUnsupportedStat> Result;
    std::lock_guard<std::mutex> Lock(GRpcUnsupportedMutex);
    Result.reserve(GRpcUnsupportedCounts.size());
    for (const auto& [Key, Count] : GRpcUnsupportedCounts)
    {
        Result.push_back(SRpcUnsupportedStat{Key.ServerType, Key.FunctionName, Count});
    }
    return Result;
}

TVector<SRpcUnsupportedStat> GetRpcUnsupportedStats(EServerType ServerType)
{
    TVector<SRpcUnsupportedStat> Result;
    std::lock_guard<std::mutex> Lock(GRpcUnsupportedMutex);
    for (const auto& [Key, Count] : GRpcUnsupportedCounts)
    {
        if (Key.ServerType == ServerType)
        {
            Result.push_back(SRpcUnsupportedStat{Key.ServerType, Key.FunctionName, Count});
        }
    }
    return Result;
}

MString BuildRpcUnsupportedStatsJson()
{
    MJsonWriter W = MJsonWriter::Array();
    for (const SRpcUnsupportedStat& Stat : GetRpcUnsupportedStats())
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

MString BuildRpcUnsupportedStatsJson(EServerType ServerType)
{
    MJsonWriter W = MJsonWriter::Array();
    for (const SRpcUnsupportedStat& Stat : GetRpcUnsupportedStats(ServerType))
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
