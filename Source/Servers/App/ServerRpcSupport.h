#pragma once

#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Object/Result.h"

namespace MServerRpcSupport
{
template<typename TResultType>
MFuture<TResultType> MakeReadyFuture(TResultType Value)
{
    MPromise<TResultType> Promise;
    MFuture<TResultType> Future = Promise.GetFuture();
    Promise.SetValue(std::move(Value));
    return Future;
}

inline bool DispatchServerCallPacket(
    MObject* Service,
    const TSharedPtr<INetConnection>& Connection,
    const TByteArray& Data)
{
    if (!Service || !Connection || Data.empty())
    {
        return false;
    }

    const uint8 PacketType = Data[0];
    if (PacketType != static_cast<uint8>(EServerMessageType::MT_FunctionCall))
    {
        return false;
    }

    const TByteArray PacketPayload(Data.begin() + 1, Data.end());

    uint16 FunctionId = 0;
    uint64 CallId = 0;
    uint32 PayloadSize = 0;
    size_t PayloadOffset = 0;
    if (!ParseServerCallPacket(PacketPayload, FunctionId, CallId, PayloadSize, PayloadOffset))
    {
        return false;
    }

    TByteArray RequestPayload;
    if (PayloadSize > 0)
    {
        RequestPayload.insert(
            RequestPayload.end(),
            PacketPayload.begin() + static_cast<TByteArray::difference_type>(PayloadOffset),
            PacketPayload.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    const TSharedPtr<IServerCallResponseTarget> ResponseTarget =
        MakeShared<MServerCallResponseTarget>(
            [Connection]() -> bool
            {
                return Connection && Connection->IsConnected();
            },
            [Connection](uint16 ResponseFunctionId, uint64 ResponseCallId, bool bSuccess, const TByteArray& ResponsePayload) -> bool
            {
                TByteArray ResponsePacketPayload;
                if (!BuildServerCallResponsePacket(
                        ResponseFunctionId,
                        ResponseCallId,
                        bSuccess,
                        ResponsePayload,
                        ResponsePacketPayload))
                {
                    return false;
                }

                return SendServerCallResponseMessage(Connection, ResponsePacketPayload);
            });

    return DispatchServerCall(Service, FunctionId, CallId, RequestPayload, ResponseTarget);
}

inline bool DispatchServerCallPacketInSubtree(
    MObject* Root,
    const TSharedPtr<INetConnection>& Connection,
    const TByteArray& Data)
{
    if (!Root || !Connection || Data.empty())
    {
        return false;
    }

    const uint8 PacketType = Data[0];
    if (PacketType != static_cast<uint8>(EServerMessageType::MT_FunctionCall))
    {
        return false;
    }

    const TByteArray PacketPayload(Data.begin() + 1, Data.end());

    uint16 FunctionId = 0;
    uint64 CallId = 0;
    uint32 PayloadSize = 0;
    size_t PayloadOffset = 0;
    if (!ParseServerCallPacket(PacketPayload, FunctionId, CallId, PayloadSize, PayloadOffset))
    {
        return false;
    }

    MObject* Target = nullptr;
    ForEachObjectInSubtree(Root, [&Target, FunctionId](MObject* Object)
    {
        if (Target)
        {
            return;
        }

        MClass* Class = Object ? Object->GetClass() : nullptr;
        if (Class && FindServerCallFunctionById(Class, FunctionId))
        {
            Target = Object;
        }
    });

    if (!Target)
    {
        return DispatchServerCallPacket(Root, Connection, Data);
    }

    return DispatchServerCallPacket(Target, Connection, Data);
}
}
