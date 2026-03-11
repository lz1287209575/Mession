#pragma once

#include "Core/NetCore.h"
#include <cstring>
#include <type_traits>
#include <utility>

template<typename T>
inline void AppendValue(TArray& OutData, const T& Value)
{
    static_assert(std::is_trivially_copyable_v<T>, "AppendValue requires trivially copyable type");

    const size_t WriteOffset = OutData.size();
    OutData.resize(WriteOffset + sizeof(T));
    memcpy(OutData.data() + WriteOffset, &Value, sizeof(T));
}

template<typename T>
inline bool ReadValue(const TArray& Data, size_t& Offset, T& OutValue)
{
    static_assert(std::is_trivially_copyable_v<T>, "ReadValue requires trivially copyable type");

    if (Offset + sizeof(T) > Data.size())
    {
        return false;
    }

    memcpy(&OutValue, Data.data() + Offset, sizeof(T));
    Offset += sizeof(T);
    return true;
}

inline void AppendString(TArray& OutData, const FString& Value)
{
    const uint16 Length = static_cast<uint16>(Value.size());
    AppendValue(OutData, Length);
    if (Length == 0)
    {
        return;
    }

    const size_t WriteOffset = OutData.size();
    OutData.resize(WriteOffset + Length);
    memcpy(OutData.data() + WriteOffset, Value.data(), Length);
}

inline bool ReadString(const TArray& Data, size_t& Offset, FString& OutValue)
{
    uint16 Length = 0;
    if (!ReadValue(Data, Offset, Length) || Offset + Length > Data.size())
    {
        return false;
    }

    OutValue.assign(reinterpret_cast<const char*>(Data.data() + Offset), Length);
    Offset += Length;
    return true;
}

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

    MMessageWriter& WriteString(const FString& Value)
    {
        AppendString(Data, Value);
        return *this;
    }

    MMessageWriter& WriteBytes(const TArray& InData)
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

    const TArray& GetData() const
    {
        return Data;
    }

    TArray&& MoveData()
    {
        return std::move(Data);
    }

private:
    TArray Data;
};

class MMessageReader
{
public:
    explicit MMessageReader(const TArray& InData)
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

    bool ReadString(FString& OutValue)
    {
        const bool bSuccess = ::ReadString(Data, Offset, OutValue);
        bValid = bValid && bSuccess;
        return bSuccess;
    }

    bool ReadBytes(size_t InSize, TArray& OutData)
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
    const TArray& Data;
    size_t Offset = 0;
    bool bValid = true;
};
