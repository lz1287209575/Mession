#pragma once

#include "Protocol/Messages/ControlPlaneMessages.h"
#include "Protocol/Messages/AuthSessionMessages.h"
#include "Protocol/Messages/SceneSyncMessages.h"
#include "Protocol/Messages/InventoryMessages.h"
#include "Protocol/Messages/ChatMessages.h"
#include "Common/Serialization/MessageReader.h"
#include "Common/Serialization/MessageWriter.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/StringUtils.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/Reflect/Reflection.h"

template<typename TMessage>
inline TByteArray BuildPayload(const TMessage& Message)
{
    if (MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TMessage))))
    {
        MReflectArchive Ar;
        TMessage MutableCopy = Message;
        StructMeta->WriteSnapshot(&MutableCopy, Ar);
        return std::move(Ar.Data);
    }
    if constexpr (requires(MMessageWriter& Writer, const TMessage& Value) { Serialize(Writer, Value); })
    {
        MMessageWriter Writer;
        Serialize(Writer, Message);
        return Writer.MoveData();
    }

    return {};
}

template<typename TMessage>
inline TResult<void, MString> ParsePayload(const TByteArray& Data, TMessage& OutMessage, const char* Context = nullptr)
{
    const size_t PayloadSize = Data.size();
    if (MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TMessage))))
    {
        MReflectArchive Ar(Data);
        StructMeta->WriteSnapshot(&OutMessage, Ar);
        if (Ar.bReadOverflow)
        {
            MString Err = (Context && Context[0]) ? (MString(Context) + ": ") : MString();
            Err += "payload_size=" + MStringUtil::ToString(static_cast<uint64>(PayloadSize)) + ", read_overflow";
            return TResult<void, MString>::Err(std::move(Err));
        }
        if (Ar.ReadPos != PayloadSize)
        {
            const size_t Trailing = PayloadSize - Ar.ReadPos;
            MString Err = (Context && Context[0]) ? (MString(Context) + ": ") : MString();
            Err += "payload_size=" + MStringUtil::ToString(static_cast<uint64>(PayloadSize)) + ", trailing_bytes=" + MStringUtil::ToString(static_cast<uint64>(Trailing));
            return TResult<void, MString>::Err(std::move(Err));
        }
        return TResult<void, MString>::Ok();
    }
    if constexpr (requires(MMessageReader& Reader, TMessage& Value) { Deserialize(Reader, Value); })
    {
        MMessageReader Reader(Data);
        if (!Deserialize(Reader, OutMessage))
        {
            MString Err = (Context && Context[0]) ? (MString(Context) + ": ") : MString();
            Err += "payload_size=" + MStringUtil::ToString(static_cast<uint64>(PayloadSize)) + ", deserialize_failed";
            return TResult<void, MString>::Err(std::move(Err));
        }
        if (!Reader.IsValid())
        {
            MString Err = (Context && Context[0]) ? (MString(Context) + ": ") : MString();
            Err += "payload_size=" + MStringUtil::ToString(static_cast<uint64>(PayloadSize)) + ", read_overflow";
            return TResult<void, MString>::Err(std::move(Err));
        }
        const size_t Trailing = Reader.GetRemainingSize();
        if (Trailing != 0)
        {
            MString Err = (Context && Context[0]) ? (MString(Context) + ": ") : MString();
            Err += "payload_size=" + MStringUtil::ToString(static_cast<uint64>(PayloadSize)) + ", trailing_bytes=" + MStringUtil::ToString(static_cast<uint64>(Trailing));
            return TResult<void, MString>::Err(std::move(Err));
        }
        return TResult<void, MString>::Ok();
    }

    MString Err = (Context && Context[0]) ? (MString(Context) + ": ") : MString();
    Err += "payload_size=" + MStringUtil::ToString(static_cast<uint64>(PayloadSize)) + ", missing_reflection_metadata";
    return TResult<void, MString>::Err(std::move(Err));
}

template<typename TMessage>
inline bool SendTypedServerMessage(MServerConnection& Connection, EServerMessageType Type, const TMessage& Message)
{
    TByteArray Payload = BuildPayload(Message);
    const uint8* PayloadData = Payload.empty() ? nullptr : Payload.data();
    return Connection.Send(static_cast<uint8>(Type), PayloadData, static_cast<uint32>(Payload.size()));
}

template<typename TMessage>
inline bool SendTypedServerMessage(const TSharedPtr<MServerConnection>& Connection, EServerMessageType Type, const TMessage& Message)
{
    if (!Connection)
    {
        return false;
    }

    return SendTypedServerMessage(*Connection, Type, Message);
}

template<typename TMessage>
inline bool SendTypedServerMessage(INetConnection& Connection, EServerMessageType Type, const TMessage& Message)
{
    TByteArray Payload = BuildPayload(Message);
    TByteArray Packet;
    Packet.reserve(1 + Payload.size());
    Packet.push_back(static_cast<uint8>(Type));
    Packet.insert(Packet.end(), Payload.begin(), Payload.end());
    return Connection.Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

template<typename TMessage>
inline bool SendTypedServerMessage(const TSharedPtr<INetConnection>& Connection, EServerMessageType Type, const TMessage& Message)
{
    if (!Connection)
    {
        return false;
    }

    return SendTypedServerMessage(*Connection, Type, Message);
}

template<auto MemberFunc>
struct TServerMessageHandlerTraits;

class MServerMessageDispatcher
{
public:
    using FHandler = TFunction<void(uint64, const TByteArray&)>;

private:
    TMap<uint8, FHandler> Handlers;

public:
    MServerMessageDispatcher() = default;

    template<typename TObject, typename TMessage>
    void Register(EServerMessageType Type, TObject* Object, void (TObject::*MemberFunc)(const TMessage&), const char* Context)
    {
        if (!Object || !MemberFunc)
        {
            return;
        }

        const uint8 TypeValue = static_cast<uint8>(Type);
        Handlers[TypeValue] = [Object, MemberFunc, Context, Type](uint64 /*ConnectionId*/, const TByteArray& Data)
        {
            TMessage Message;
            auto ParseResult = ParsePayload(Data, Message, Context);
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ServerMessageDispatcher ParsePayload failed for type %d: %s",
                         static_cast<int>(Type),
                         ParseResult.GetError().c_str());
                return;
            }

            (Object->*MemberFunc)(Message);
        };
    }

    template<auto MemberFunc, typename TObject>
    void RegisterAuto(EServerMessageType Type, TObject* Object, const char* Context)
    {
        using Traits = TServerMessageHandlerTraits<MemberFunc>;
        static_assert(std::is_same_v<typename Traits::ObjectType, TObject>,
                      "RegisterAuto object type does not match member function owner");

        if (!Object)
        {
            return;
        }

        const uint8 TypeValue = static_cast<uint8>(Type);
        Handlers[TypeValue] = [Object, Context, Type](uint64 ConnectionId, const TByteArray& Data)
        {
            if constexpr (Traits::bUsesRawData)
            {
                Traits::Invoke(Object, ConnectionId, Data);
            }
            else
            {
                using TMessage = typename Traits::MessageType;
                TMessage Message;
                auto ParseResult = ParsePayload(Data, Message, Context);
                if (!ParseResult.IsOk())
                {
                    LOG_WARN("ServerMessageDispatcher ParsePayload failed for type %d: %s",
                             static_cast<int>(Type),
                             ParseResult.GetError().c_str());
                    return;
                }

                Traits::Invoke(Object, ConnectionId, Message);
            }
        };
    }

    void Dispatch(uint8 Type, const TByteArray& Data) const
    {
        Dispatch(0, Type, Data);
    }

    void Dispatch(uint64 ConnectionId, uint8 Type, const TByteArray& Data) const
    {
        auto It = Handlers.find(Type);
        if (It == Handlers.end())
        {
            LOG_DEBUG("ServerMessageDispatcher has no handler for type %u",
                      static_cast<unsigned>(Type));
            return;
        }

        It->second(ConnectionId, Data);
    }
};

template<typename TObject, typename TMessage, void (TObject::*MemberFunc)(const TMessage&)>
struct TServerMessageHandlerTraits<MemberFunc>
{
    using ObjectType = TObject;
    using MessageType = TMessage;
    static constexpr bool bUsesRawData = std::is_same_v<std::remove_cv_t<std::remove_reference_t<TMessage>>, TByteArray>;

    static void Invoke(TObject* Object, uint64 /*ConnectionId*/, const TMessage& Message)
    {
        (Object->*MemberFunc)(Message);
    }
};

template<typename TObject, typename TMessage, void (TObject::*MemberFunc)(uint64, const TMessage&)>
struct TServerMessageHandlerTraits<MemberFunc>
{
    using ObjectType = TObject;
    using MessageType = TMessage;
    static constexpr bool bUsesRawData = std::is_same_v<std::remove_cv_t<std::remove_reference_t<TMessage>>, TByteArray>;

    static void Invoke(TObject* Object, uint64 ConnectionId, const TMessage& Message)
    {
        (Object->*MemberFunc)(ConnectionId, Message);
    }
};

#define MREGISTER_SERVER_MESSAGE_HANDLER(Dispatcher, Type, Method, Context) \
    (Dispatcher).RegisterAuto<Method>(Type, this, Context)
