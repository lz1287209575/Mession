#pragma once

#include "Common/Net/Rpc/RpcDispatch.h"
#include "Common/Net/Rpc/RpcTransport.h"

#include <utility>

struct SClientCallContext
{
    uint64 ConnectionId = 0;
    uint16 FunctionId = 0;
    uint64 CallId = 0;
    TSharedPtr<IClientResponseTarget> ResponseTarget;

    bool IsValid() const
    {
        return ConnectionId != 0 && FunctionId != 0 && ResponseTarget != nullptr;
    }
};

uint64 GetCurrentClientConnectionId();
uint64 GetCurrentClientCallId();
SClientCallContext CaptureCurrentClientCallContext();
void RegisterDeferredClientCall(const SClientCallContext& Context);
void UnregisterDeferredClientCall(const SClientCallContext& Context);
bool IsDeferredClientCall(const SClientCallContext& Context);
void MarkCurrentClientCallDeferred();
bool IsCurrentClientCallDeferred();
bool SendDeferredClientCallResponse(const SClientCallContext& Context, const TByteArray& Payload);

uint16 GetClientDownlinkFunctionId(const char* FunctionName);
const char* GetClientDownlinkFunctionName(uint16 FunctionId);
const MFunction* FindClientDownlinkFunctionById(uint16 FunctionId);
const MFunction* FindClientDownlinkFunctionByName(const char* FunctionName);

inline bool BuildClientFunctionArgsPayload(const MFunction* Function, TByteArray& OutPayload)
{
    if (!Function)
    {
        return false;
    }

    MReflectArchive Ar;
    if (!SerializeFunctionArgsByMeta(Function, Ar))
    {
        return false;
    }

    OutPayload = std::move(Ar.Data);
    return true;
}

template<typename... TArgs>
inline bool BuildClientFunctionArgsPayload(const MFunction* Function, TByteArray& OutPayload, TArgs&&... Args)
{
    if (!Function)
    {
        return false;
    }

    MReflectArchive Ar;
    if (!SerializeFunctionArgsByMeta(Function, Ar, std::forward<TArgs>(Args)...))
    {
        return false;
    }

    OutPayload = std::move(Ar.Data);
    return true;
}

template<typename... TArgs>
inline bool BuildClientFunctionCallPacketById(uint16 FunctionId, TByteArray& OutPacket, TArgs&&... Args)
{
    const MFunction* Function = FindClientDownlinkFunctionById(FunctionId);
    TByteArray Payload;
    if (!BuildClientFunctionArgsPayload(Function, Payload, std::forward<TArgs>(Args)...))
    {
        return false;
    }

    return BuildClientFunctionPacket(FunctionId, Payload, OutPacket);
}

template<typename... TArgs>
inline bool BuildClientFunctionCallPacketByName(const char* FunctionName, TByteArray& OutPacket, TArgs&&... Args)
{
    const MFunction* Function = FindClientDownlinkFunctionByName(FunctionName);
    if (!Function)
    {
        return false;
    }

    return BuildClientFunctionCallPacketById(Function->FunctionId, OutPacket, std::forward<TArgs>(Args)...);
}

template<typename TResponse>
inline bool SendDeferredClientCallResponse(const SClientCallContext& Context, const TResponse& Response)
{
    if (!Context.IsValid())
    {
        return false;
    }

    const TByteArray Payload = BuildPayload(Response);
    return SendDeferredClientCallResponse(Context, Payload);
}
