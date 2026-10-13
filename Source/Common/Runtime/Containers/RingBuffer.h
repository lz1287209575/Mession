/**
 * @file RingBuffer.h
 * @brief MRingBuffer - 定长环形缓冲,满时新元素挤掉最旧元素。
 *
 * @detail 语义与陷阱:
 *   - 容量在构造期固定(<= 0 会被抬到 1),不扩容;底层 TVector<T> 一次性分配,
 *     因此要求 T 可默认构造;
 *   - PushBack 在满时覆盖最旧元素并前移 Start —— 这是"有损"队列,不报错也不失败,
 *     不能接受丢数据的调用方要先用 Full() 判断;
 *   - 逻辑索引约定:0 = 最旧,GetSize() - 1 = 最新(与物理下标无关,内部取模换算);
 *   - At()/Front()/Back() 不做越界检查,调用方须保证 Index < GetSize();
 *     容器为空时 Front()/Back() 返回 nullptr;
 *   - 非线程安全,无内部加锁。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include <utility>

/**
 * @brief MRingBuffer - 固定容量环形队列。
 *
 * @param InCapacity 容量;传 0 会退化为容量 1(避免除零/空缓冲)
 */
template <typename T> class MRingBuffer {
    public:
    explicit MRingBuffer(size_t InCapacity) : Capacity(InCapacity > 0 ? InCapacity : 1), Buffer(Capacity), Start(0), Count(0) {
    }

    /**
     * @brief PushBack - 拷贝入队。
     * @param Value 待入队元素
     *
     * @detail 未满时追加到尾部;已满时覆盖最旧元素(最旧者被丢弃)。
     */
    void PushBack(const T& Value) {
        if (Count < Capacity) {
            Buffer[(Start + Count) % Capacity] = Value;
            ++Count;
        } else {
            Buffer[Start] = Value;
            Start         = (Start + 1) % Capacity;
        }
    }

    /**
     * @brief PushBack - 移动入队。
     * @param Value 待入队元素(移动后源对象状态未指定)
     */
    void PushBack(T&& Value) {
        if (Count < Capacity) {
            Buffer[(Start + Count) % Capacity] = std::move(Value);
            ++Count;
        } else {
            Buffer[Start] = std::move(Value);
            Start         = (Start + 1) % Capacity;
        }
    }

    /**
     * @brief PopFront - 弹出最旧元素。
     * @return 最旧元素的副本/移动值;容器为空时返回空的 TOptional
     *
     * @detail 弹出位置的对象被移动走后不会析构重置,其内容在下次被覆盖前一直残留在
     * 底层数组里 —— 敏感数据不应放进本容器。
     */
    TOptional<T> PopFront() {
        if (Count == 0) {
            return TOptional<T>();
        }
        T Value = std::move(Buffer[Start]);
        Start   = (Start + 1) % Capacity;
        --Count;
        return Value;
    }

    bool Empty() const {
        return Count == 0;
    }
    bool Full() const {
        return Count == Capacity;
    }
    size_t GetSize() const {
        return Count;
    }
    size_t GetCapacity() const {
        return Capacity;
    }

    /**
     * @brief Clear - 逻辑清空。
     *
     * @detail 只重置游标与计数,不析构/不清零底层已弹出的元素。
     */
    void Clear() {
        Start = 0;
        Count = 0;
    }

    /**
     * @brief Front - 最旧元素(逻辑索引 0)。
     * @return 指针;容器为空时返回 nullptr。非 const 重载允许就地改写。
     */
    T* Front() {
        if (Count == 0) {
            return nullptr;
        }
        return &Buffer[Start];
    }
    const T* Front() const {
        if (Count == 0) {
            return nullptr;
        }
        return &Buffer[Start];
    }

    /**
     * @brief Back - 最新元素(逻辑索引 GetSize() - 1)。
     * @return 指针;容器为空时返回 nullptr
     */
    T* Back() {
        if (Count == 0) {
            return nullptr;
        }
        return &Buffer[(Start + Count - 1) % Capacity];
    }
    const T* Back() const {
        if (Count == 0) {
            return nullptr;
        }
        return &Buffer[(Start + Count - 1) % Capacity];
    }

    /**
     * @brief At - 按逻辑索引随机访问。
     * @param Index 0 = 最旧,GetSize() - 1 = 最新
     * @return 元素引用
     *
     * @detail 无越界检查:Index >= GetSize() 会读到尚未入队或已被覆盖的槽位。
     */
    T& At(size_t Index) {
        return Buffer[(Start + Index) % Capacity];
    }
    const T& At(size_t Index) const {
        return Buffer[(Start + Index) % Capacity];
    }

    private:
    const size_t Capacity;
    TVector<T>   Buffer;
    size_t       Start;
    size_t       Count;
};
