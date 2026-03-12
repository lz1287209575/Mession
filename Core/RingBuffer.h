#pragma once

#include "Core/NetCore.h"
#include <utility>

// 定长环形缓冲区：满时 PushBack 覆盖最旧元素，PopFront 取出最旧
template<typename T>
class MRingBuffer
{
public:
    explicit MRingBuffer(size_t InCapacity)
        : Capacity(InCapacity > 0 ? InCapacity : 1)
        , Buffer(Capacity)
        , Start(0)
        , Count(0)
    {
    }

    void PushBack(const T& Value)
    {
        if (Count < Capacity)
        {
            Buffer[(Start + Count) % Capacity] = Value;
            ++Count;
        }
        else
        {
            Buffer[Start] = Value;
            Start = (Start + 1) % Capacity;
        }
    }

    void PushBack(T&& Value)
    {
        if (Count < Capacity)
        {
            Buffer[(Start + Count) % Capacity] = std::move(Value);
            ++Count;
        }
        else
        {
            Buffer[Start] = std::move(Value);
            Start = (Start + 1) % Capacity;
        }
    }

    TOptional<T> PopFront()
    {
        if (Count == 0)
        {
            return TOptional<T>();
        }
        T Value = std::move(Buffer[Start]);
        Start = (Start + 1) % Capacity;
        --Count;
        return Value;
    }

    bool Empty() const { return Count == 0; }
    bool Full() const { return Count == Capacity; }
    size_t GetSize() const { return Count; }
    size_t GetCapacity() const { return Capacity; }

    void Clear()
    {
        Start = 0;
        Count = 0;
    }

    // 最旧元素（索引 0）
    T* Front()
    {
        if (Count == 0)
        {
            return nullptr;
        }
        return &Buffer[Start];
    }
    const T* Front() const
    {
        if (Count == 0)
        {
            return nullptr;
        }
        return &Buffer[Start];
    }

    // 最新元素
    T* Back()
    {
        if (Count == 0)
        {
            return nullptr;
        }
        return &Buffer[(Start + Count - 1) % Capacity];
    }
    const T* Back() const
    {
        if (Count == 0)
        {
            return nullptr;
        }
        return &Buffer[(Start + Count - 1) % Capacity];
    }

    // 逻辑索引：0 = 最旧，GetSize()-1 = 最新
    T& At(size_t Index)
    {
        return Buffer[(Start + Index) % Capacity];
    }
    const T& At(size_t Index) const
    {
        return Buffer[(Start + Index) % Capacity];
    }

private:
    const size_t Capacity;
    TVector<T> Buffer;
    size_t Start;
    size_t Count;
};
