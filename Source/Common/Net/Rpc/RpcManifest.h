#pragma once

#include "Common/Net/Rpc/RpcTransport.h"
#include "Common/Runtime/Reflect/Reflection.h"

#include <cstddef>
#include <utility>

struct SRpcEndpointBinding {
    EServerType ServerType   = EServerType::Unknown;
    const char* ClassName    = nullptr;
    const char* FunctionName = nullptr;
};

const char* GetServerTypeDisplayName(EServerType ServerType);
const char* GetServerEndpointClassName(EServerType ServerType);
bool        FindRpcEndpoint(EServerType ServerType, const char* FunctionName, SRpcEndpointBinding& OutBinding);
void        ReportUnsupportedRpcEndpoint(EServerType ServerType, const char* FunctionName);

template <typename... TArgs> inline bool BuildRpcPayloadForRemoteCall(const char* ClassName, const char* FunctionName, TByteArray& OutData, TArgs&&... Args) {
    if (!ClassName || !FunctionName) {
        return false;
    }

    if (MClass* Class = MObject::FindClass(ClassName)) {
        if (MFunction* Func = Class->FindFunction(FunctionName)) {
            return BuildRpcPayloadForFunctionCall(Func, OutData, std::forward<TArgs>(Args)...);
        }
    }

    return BuildServerRpcPayload(MGET_STABLE_RPC_FUNCTION_ID(ClassName, FunctionName), BuildRpcArgsPayload(std::forward<TArgs>(Args)...), OutData);
}

namespace MRpc {
    template <typename... TArgs> inline bool CallRemote(MServerConnection& Connection, const char* ClassName, const char* FunctionName, TArgs&&... Args) {
        TByteArray RpcPayload;
        if (!BuildRpcPayloadForRemoteCall(ClassName, FunctionName, RpcPayload, std::forward<TArgs>(Args)...)) {
            return false;
        }

        if (!SendServerRpcMessage(Connection, RpcPayload)) {
            return false;
        }
        return true;
    }

    template <typename... TArgs> inline bool CallRemote(const TSharedPtr<MServerConnection>& Connection, const char* ClassName, const char* FunctionName, TArgs&&... Args) {
        if (!Connection) {
            return false;
        }

        return CallRemote(*Connection, ClassName, FunctionName, std::forward<TArgs>(Args)...);
    }

    template <typename... TArgs> inline bool CallRemote(INetConnection& Connection, const char* ClassName, const char* FunctionName, TArgs&&... Args) {
        TByteArray RpcPayload;
        if (!BuildRpcPayloadForRemoteCall(ClassName, FunctionName, RpcPayload, std::forward<TArgs>(Args)...)) {
            return false;
        }

        if (!SendServerRpcMessage(Connection, RpcPayload)) {
            return false;
        }
        return true;
    }

    template <typename... TArgs> inline bool CallRemote(const TSharedPtr<INetConnection>& Connection, const char* ClassName, const char* FunctionName, TArgs&&... Args) {
        if (!Connection) {
            return false;
        }

        return CallRemote(*Connection, ClassName, FunctionName, std::forward<TArgs>(Args)...);
    }

} // namespace MRpc
