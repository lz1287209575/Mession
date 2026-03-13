#include "NetDriver/ServerRpcServices.h"
#include "Common/Logger.h"

namespace
{
FWorldSessionValidateResponseHandler GWorldSessionValidateResponseHandler;
uint16 GWorldSessionValidateResponseFunctionId = 0;

void WorldService_SessionValidateResponse_Invoker(MReflectObject* Object, MReflectArchive& Ar)
{
    if (!Object)
    {
        return;
    }

    auto* Service = static_cast<MWorldService*>(Object);
    uint64 ConnectionId = 0;
    uint64 PlayerId = 0;
    bool bValid = false;

    Ar << ConnectionId;
    Ar << PlayerId;
    Ar << bValid;

    Service->Rpc_OnSessionValidateResponse(ConnectionId, PlayerId, bValid);
}
} // namespace

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
    if (!FuncMeta || !FuncMeta->RpcFunc)
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
    FuncMeta->RpcFunc(ServiceInstance, Ar);
    return true;
}

void MWorldService::Rpc_OnSessionValidateResponse(uint64 ConnectionId, uint64 PlayerId, bool bValid)
{
    if (GWorldSessionValidateResponseHandler)
    {
        GWorldSessionValidateResponseHandler(ConnectionId, PlayerId, bValid);
    }
    else
    {
        LOG_WARN("MWorldService Rpc_OnSessionValidateResponse with no handler bound (ConnId=%llu, PlayerId=%llu, bValid=%d)",
                 (unsigned long long)ConnectionId,
                 (unsigned long long)PlayerId,
                 bValid ? 1 : 0);
    }
}

void MWorldService::RegisterAllProperties(MClass* InClass)
{
    (void)InClass;
}

void MWorldService::RegisterAllFunctions(MClass* InClass)
{
    if (!InClass)
    {
        return;
    }

    MFunction* Func = new MFunction();
    Func->Name = "Rpc_OnSessionValidateResponse";
    Func->Flags = EFunctionFlags::NetServer;
    Func->RpcType = ERpcType::ServerToServer;
    Func->bReliable = true;
    Func->RpcFunc = &WorldService_SessionValidateResponse_Invoker;
    InClass->RegisterFunction(Func);
}

IMPLEMENT_CLASS(MWorldService, MReflectObject, 0)

void SetWorldSessionValidateResponseHandler(const FWorldSessionValidateResponseHandler& InHandler)
{
    GWorldSessionValidateResponseHandler = InHandler;
}

uint16 GetWorldSessionValidateResponseFunctionId()
{
    if (GWorldSessionValidateResponseFunctionId != 0)
    {
        return GWorldSessionValidateResponseFunctionId;
    }

    MClass* Class = MWorldService::StaticClass();
    if (!Class)
    {
        return 0;
    }

    MFunction* Func = Class->FindFunction("Rpc_OnSessionValidateResponse");
    if (!Func)
    {
        return 0;
    }

    GWorldSessionValidateResponseFunctionId = Func->FunctionId;
    return GWorldSessionValidateResponseFunctionId;
}
