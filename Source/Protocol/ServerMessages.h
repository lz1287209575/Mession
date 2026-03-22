#pragma once

#include "Protocol/Messages/ControlPlaneMessages.h"
#include "Protocol/Messages/AppMessages.h"
#include "Protocol/Messages/AuthSessionMessages.h"
#include "Protocol/Messages/SceneSyncMessages.h"
#include "Protocol/Messages/InventoryMessages.h"
#include "Protocol/Messages/ChatMessages.h"
#include "Protocol/Messages/ClientCallMessages.h"
#include "Protocol/Messages/WorldPlayerMessages.h"
#include "Common/Serialization/MessageReader.h"
#include "Common/Serialization/MessageWriter.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/StringUtils.h"
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
