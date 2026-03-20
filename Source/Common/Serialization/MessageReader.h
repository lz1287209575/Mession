#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Serialization/MessageUtils.h"


class MMessageReader
{
public:
    explicit MMessageReader(const TByteArray& InData)
        : Data(InData)
    {
    }

    template<typename T>
    bool Read(T& OutValue)
    {
        const bool bSuccess = ReadValue(Data, Offset, OutValue);
        bValid = bValid && bSuccess;
        return bSuccess;
    }

    bool ReadBE(uint16& OutValue)
    {
        const bool bSuccess = ReadValueBE(Data, Offset, OutValue);
        bValid = bValid && bSuccess;
        return bSuccess;
    }

    bool ReadBE(uint32& OutValue)
    {
        const bool bSuccess = ReadValueBE(Data, Offset, OutValue);
        bValid = bValid && bSuccess;
        return bSuccess;
    }

    bool ReadBE(uint64& OutValue)
    {
        const bool bSuccess = ReadValueBE(Data, Offset, OutValue);
        bValid = bValid && bSuccess;
        return bSuccess;
    }

    bool ReadString(MString& OutValue)
    {
        const bool bSuccess = ::ReadString(Data, Offset, OutValue);
        bValid = bValid && bSuccess;
        return bSuccess;
    }

    bool ReadStringBE(MString& OutValue)
    {
        const bool bSuccess = ::ReadStringBE(Data, Offset, OutValue);
        bValid = bValid && bSuccess;
        return bSuccess;
    }

    bool ReadBytes(size_t InSize, TByteArray& OutData)
    {
        if (Offset + InSize > Data.size())
        {
            bValid = false;
            return false;
        }

        OutData.assign(Data.begin() + Offset, Data.begin() + Offset + InSize);
        Offset += InSize;
        return true;
    }

    bool CanRead(size_t InSize) const
    {
        return Offset + InSize <= Data.size();
    }

    size_t GetOffset() const
    {
        return Offset;
    }

    size_t GetRemainingSize() const
    {
        return (Offset <= Data.size()) ? (Data.size() - Offset) : 0;
    }

    bool IsValid() const
    {
        return bValid;
    }

private:
    const TByteArray& Data;
    size_t Offset = 0;
    bool bValid = true;
};
