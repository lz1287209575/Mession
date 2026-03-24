#include "Common/Net/Rpc/RpcClientCall.h"

#include "Common/Net/ClientDownlink.h"
#include <mutex>

thread_local uint64 GCurrentClientConnectionId = 0;
thread_local SClientCallContext GCurrentClientCallContext;

namespace
{
struct SDeferredClientCallKey
{
    uint64 ConnectionId = 0;
    uint16 FunctionId = 0;
    uint64 CallId = 0;

    bool operator<(const SDeferredClientCallKey& Other) const
    {
        if (ConnectionId != Other.ConnectionId)
        {
            return ConnectionId < Other.ConnectionId;
        }
        if (FunctionId != Other.FunctionId)
        {
            return FunctionId < Other.FunctionId;
        }
        return CallId < Other.CallId;
    }
};

SDeferredClientCallKey BuildDeferredClientCallKey(const SClientCallContext& Context)
{
    return SDeferredClientCallKey{
        Context.ConnectionId,
        Context.FunctionId,
        Context.CallId,
    };
}

std::mutex GDeferredClientCallMutex;
TSet<SDeferredClientCallKey> GDeferredClientCalls;
}

uint64 GetCurrentClientConnectionId()
{
    return GCurrentClientConnectionId;
}

uint64 GetCurrentClientCallId()
{
    return GCurrentClientCallContext.CallId;
}

SClientCallContext CaptureCurrentClientCallContext()
{
    return GCurrentClientCallContext;
}

void RegisterDeferredClientCall(const SClientCallContext& Context)
{
    if (!Context.IsValid())
    {
        return;
    }

    std::lock_guard<std::mutex> Lock(GDeferredClientCallMutex);
    GDeferredClientCalls.insert(BuildDeferredClientCallKey(Context));
}

void UnregisterDeferredClientCall(const SClientCallContext& Context)
{
    if (!Context.IsValid())
    {
        return;
    }

    std::lock_guard<std::mutex> Lock(GDeferredClientCallMutex);
    GDeferredClientCalls.erase(BuildDeferredClientCallKey(Context));
}

bool IsDeferredClientCall(const SClientCallContext& Context)
{
    if (!Context.IsValid())
    {
        return false;
    }

    std::lock_guard<std::mutex> Lock(GDeferredClientCallMutex);
    return GDeferredClientCalls.find(BuildDeferredClientCallKey(Context)) != GDeferredClientCalls.end();
}

void MarkCurrentClientCallDeferred()
{
    RegisterDeferredClientCall(GCurrentClientCallContext);
}

bool IsCurrentClientCallDeferred()
{
    return IsDeferredClientCall(GCurrentClientCallContext);
}

bool SendDeferredClientCallResponse(const SClientCallContext& Context, const TByteArray& Payload)
{
    if (!Context.IsValid())
    {
        return false;
    }

    if (!Context.ResponseTarget->CanSendClientResponse(Context.ConnectionId))
    {
        UnregisterDeferredClientCall(Context);
        return false;
    }

    const bool bSent = Context.ResponseTarget->SendClientResponse(
        Context.ConnectionId,
        Context.FunctionId,
        Context.CallId,
        Payload);
    UnregisterDeferredClientCall(Context);
    return bSent;
}

uint16 GetClientDownlinkFunctionId(const char* FunctionName)
{
    const MFunction* Function = FindClientDownlinkFunctionByName(FunctionName);
    return Function ? Function->FunctionId : 0;
}

const char* GetClientDownlinkFunctionName(uint16 FunctionId)
{
    const MFunction* Function = FindClientDownlinkFunctionById(FunctionId);
    return Function ? Function->Name.c_str() : nullptr;
}

const MFunction* FindClientDownlinkFunctionById(uint16 FunctionId)
{
    if (FunctionId == 0)
    {
        return nullptr;
    }

    MClass* DownlinkClass = MClientDownlink::StaticClass();
    return DownlinkClass ? DownlinkClass->FindFunctionById(FunctionId) : nullptr;
}

const MFunction* FindClientDownlinkFunctionByName(const char* FunctionName)
{
    if (!FunctionName || FunctionName[0] == '\0')
    {
        return nullptr;
    }

    MClass* DownlinkClass = MClientDownlink::StaticClass();
    return DownlinkClass ? DownlinkClass->FindFunction(FunctionName) : nullptr;
}
