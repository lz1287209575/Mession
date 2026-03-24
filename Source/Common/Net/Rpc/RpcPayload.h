#pragma once

#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/StringUtils.h"
#include "Common/Runtime/Reflect/Reflection.h"

template<typename TMessage>
inline TByteArray BuildPayload(const TMessage& Message)
{
    if (MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TMessage))))
    {
        MReflectArchive Archive;
        TMessage MutableCopy = Message;
        StructMeta->WriteSnapshot(&MutableCopy, Archive);
        return std::move(Archive.Data);
    }

    return {};
}

template<typename TMessage>
inline TResult<void, MString> ParsePayload(
    const TByteArray& Data,
    TMessage& OutMessage,
    const char* Context = nullptr)
{
    const size_t PayloadSize = Data.size();
    if (MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TMessage))))
    {
        MReflectArchive Archive(Data);
        StructMeta->WriteSnapshot(&OutMessage, Archive);
        if (Archive.bReadOverflow)
        {
            MString Error = (Context && Context[0]) ? (MString(Context) + ": ") : MString();
            Error += "payload_size=" + MStringUtil::ToString(static_cast<uint64>(PayloadSize)) + ", read_overflow";
            return TResult<void, MString>::Err(std::move(Error));
        }
        if (Archive.ReadPos != PayloadSize)
        {
            const size_t Trailing = PayloadSize - Archive.ReadPos;
            MString Error = (Context && Context[0]) ? (MString(Context) + ": ") : MString();
            Error += "payload_size=" + MStringUtil::ToString(static_cast<uint64>(PayloadSize)) +
                     ", trailing_bytes=" + MStringUtil::ToString(static_cast<uint64>(Trailing));
            return TResult<void, MString>::Err(std::move(Error));
        }
        return TResult<void, MString>::Ok();
    }

    MString Error = (Context && Context[0]) ? (MString(Context) + ": ") : MString();
    Error += "payload_size=" + MStringUtil::ToString(static_cast<uint64>(PayloadSize)) + ", missing_reflection_metadata";
    return TResult<void, MString>::Err(std::move(Error));
}
