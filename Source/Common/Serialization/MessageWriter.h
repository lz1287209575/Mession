#pragma once

#include "Common/Runtime/MLib.h"

#include "Common/Serialization/MessageUtils.h"

class MMessageWriter
{
public:
    MMessageWriter() = default;

    explicit MMessageWriter(size_t ReserveSize)
    {
        Data.reserve(ReserveSize);
    }

    template<typename T>
    MMessageWriter& Write(const T& Value)
    {
        AppendValue(Data, Value);
        return *this;
    }

    MMessageWriter& WriteBE(uint16 Value)
    {
        AppendValueBE(Data, Value);
        return *this;
    }

    MMessageWriter& WriteBE(uint32 Value)
    {
        AppendValueBE(Data, Value);
        return *this;
    }

    MMessageWriter& WriteBE(uint64 Value)
    {
        AppendValueBE(Data, Value);
        return *this;
    }

    MMessageWriter& WriteString(const MString& Value)
    {
        AppendString(Data, Value);
        return *this;
    }

    MMessageWriter& WriteStringBE(const MString& Value)
    {
        AppendStringBE(Data, Value);
        return *this;
    }

    MMessageWriter& WriteBytes(const TByteArray& InData)
    {
        Data.insert(Data.end(), InData.begin(), InData.end());
        return *this;
    }

    MMessageWriter& WriteBytes(const uint8* InData, size_t InSize)
    {
        if (InData && InSize > 0)
        {
            Data.insert(Data.end(), InData, InData + InSize);
        }
        return *this;
    }

    const TByteArray& GetData() const
    {
        return Data;
    }

    TByteArray&& MoveData()
    {
        return std::move(Data);
    }

private:
    TByteArray Data;
};
