#include "Common/Net/Rpc/RpcManifest.h"

#include "Common/Runtime/Log/Log.h"

#include <mutex>

namespace {
    struct SRpcUnsupportedKey {
        EServerType ServerType = EServerType::Unknown;
        MString     FunctionName;

        bool operator<(const SRpcUnsupportedKey& Other) const {
            if (ServerType != Other.ServerType) {
                return static_cast<uint8>(ServerType) < static_cast<uint8>(Other.ServerType);
            }

            return FunctionName < Other.FunctionName;
        }
    };

    std::mutex                       GRpcUnsupportedMutex;
    TMap<SRpcUnsupportedKey, uint64> GRpcUnsupportedCounts;
} // namespace

const char* GetServerTypeDisplayName(EServerType ServerType) {
    switch (ServerType) {
    case EServerType::Gateway:
        return "Gateway";
    case EServerType::Echo:
        return "Echo";
    default:
        return "Unknown";
    }
}

const char* GetServerEndpointClassName(EServerType ServerType) {
    switch (ServerType) {
    case EServerType::Gateway:
        return "MGatewayServer";
    case EServerType::Echo:
        return "MEchoService";
    default:
        return nullptr;
    }
}

bool FindRpcEndpoint(EServerType ServerType, const char* FunctionName, SRpcEndpointBinding& OutBinding) {
    if (!FunctionName) {
        return false;
    }

    const TVector<MClass*> Classes = MObject::GetAllClasses();
    for (MClass* Class : Classes) {
        if (!Class) {
            continue;
        }

        for (MFunction* Function : Class->GetFunctions()) {
            if (!Function) {
                continue;
            }
            if (Function->EndpointServerType != ServerType) {
                continue;
            }
            if (Function->Name != FunctionName) {
                continue;
            }

            OutBinding.ServerType   = ServerType;
            OutBinding.ClassName    = Class->GetName().c_str();
            OutBinding.FunctionName = Function->Name.c_str();
            return true;
        }
    }

    return false;
}

void ReportUnsupportedRpcEndpoint(EServerType ServerType, const char* FunctionName) {
    const MString Name = FunctionName ? MString(FunctionName) : MString();
    if (Name.empty()) {
        return;
    }

    uint64 Count = 0;
    {
        std::lock_guard<std::mutex> Lock(GRpcUnsupportedMutex);
        Count = ++GRpcUnsupportedCounts[{ServerType, Name}];
    }

    if (Count == 1 || Count == 10 || Count == 100) {
        LOG_WARN("RPC endpoint unsupported: target=%s function=%s count=%llu", GetServerTypeDisplayName(ServerType), Name.c_str(), static_cast<unsigned long long>(Count));
    }
}
