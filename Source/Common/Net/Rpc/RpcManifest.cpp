/**
 * @file RpcManifest.cpp
 * @brief RpcManifest 的实现:ServerType ↔ 显示名/类名映射,反射表端点反查,限频告警。
 */

#include "Common/Net/Rpc/RpcManifest.h"

#include "Common/Runtime/Log/Log.h"

#include <mutex>

namespace {
    /// 不支持端点的计数键;先按 ServerType 再按函数名排序,保证 TMap 有序且稳定。
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

    // 告警计数表按 (ServerType, 函数名) 累计;用独立 mutex 保护,进程内不清理。
    // 之所以要计数,是因为这类失败通常由"配置写错/版本不匹配"引起,会在热路径上反复发生。
    std::mutex                       GRpcUnsupportedMutex;
    TMap<SRpcUnsupportedKey, uint64> GRpcUnsupportedCounts;
} // namespace

const char* GetServerTypeDisplayName(EServerType ServerType) {
    switch (ServerType) {
    case EServerType::Gateway:
        return "Gateway";
    case EServerType::Echo:
        return "Echo";
    case EServerType::Player:
        return "Player";
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
    case EServerType::Player:
        return "MPlayerService";
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

            // 注意:这里把反射系统内部的字符串指针直接交给调用方(不拷贝)。
            // 类与函数的元数据在整个进程生命周期内稳定存活,因此指针始终有效。
            OutBinding.ServerType   = ServerType;
            OutBinding.ClassName    = Class->GetName().c_str();
            OutBinding.FunctionName = Function->Name.c_str();
            return true;
        }
    }

    return false;
}

// 只在第 1 / 10 / 100 次打 WARN:既能立刻暴露配置错误,又不会把日志刷爆。
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
