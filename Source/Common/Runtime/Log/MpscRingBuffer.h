#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

// TMpscRingBuffer<T>: 多生产者单消费者环形队列。
// 容量必须是 2 的幂（内部使用位掩码取模）。
// 同步机制: 三个计数器
//   - EnqueuePos (atomic): 已抢占的 slot 上界(含尚未发布的)
//   - PublishedHead (atomic): 已发布的上界(消费者按此读取)
//   - DequeuePos (单线程): 已消费的下界
// 多生产者通过 CAS(EnqueuePos) 抢占下一个可写 slot;
// 写入 Storage 后发布(推进 PublishedHead);消费者读取 PublishedHead 范围内的 slot。
template<typename T>
class TMpscRingBuffer
{
public:
    explicit TMpscRingBuffer(size_t CapacityPow2)
        : CapacityMask(CapacityPow2 - 1)
    {
        SlotCount = CapacityPow2;
        Slots = new Slot[SlotCount];
        for (size_t i = 0; i < SlotCount; ++i)
        {
            new (&Slots[i].Storage) T();
        }
        EnqueuePos.store(0, std::memory_order_relaxed);
        PublishedHead.store(0, std::memory_order_relaxed);
        DequeuePos = 0;
    }

    ~TMpscRingBuffer()
    {
        for (size_t i = 0; i < SlotCount; ++i)
        {
            Slots[i].Storage.~T();
        }
        delete[] Slots;
    }

    bool TryEnqueue(const T& Record, EEvictionPolicy Policy = EEvictionPolicy::DropOldest)
    {
        (void)Policy;
        // 多生产者: CAS 抢占下一个可写位置。
        size_t Cur = EnqueuePos.load(std::memory_order_relaxed);
        while (true)
        {
            // 满条件:已发布数 >= Capacity,即 (PublishedHead - DequeuePos) >= Capacity。
            size_t Tail = DequeuePos;
            size_t Published = PublishedHead.load(std::memory_order_acquire);
            if (Published - Tail >= Capacity())
            {
                return false;
            }
            if (EnqueuePos.compare_exchange_weak(Cur, Cur + 1,
                    std::memory_order_relaxed, std::memory_order_relaxed))
            {
                break;
            }
            // CAS 失败时 Cur 已被更新,循环重试。
        }
        // 此时 Cur 是我们抢占的 slot 索引。
        size_t Idx = Cur & CapacityMask;
        Slots[Idx].Storage = Record;
        // Release fence: Storage 写入先于 PublishedHead 推进对消费者可见。
        std::atomic_thread_fence(std::memory_order_release);
        // 推进 PublishedHead (单调递增)。
        size_t CurPub = PublishedHead.load(std::memory_order_relaxed);
        size_t Target = Cur + 1;
        while (CurPub < Target)
        {
            if (PublishedHead.compare_exchange_weak(CurPub, Target,
                    std::memory_order_release, std::memory_order_relaxed))
            {
                break;
            }
        }
        return true;
    }

    // 阻塞 enqueue（ERROR/CRITICAL 用，带超时）。
    bool BlockingEnqueue(const T& Record, int TimeoutMs = 100)
    {
        auto Deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(TimeoutMs);
        while (std::chrono::steady_clock::now() < Deadline)
        {
            if (TryEnqueue(Record))
            {
                return true;
            }
            std::this_thread::yield();
        }
        return false;
    }

    // 消费者批量拉取；返回实际写入 OutBuffer 的数量。
    size_t DequeueBatch(T* OutBuffer, size_t MaxCount)
    {
        size_t Published = PublishedHead.load(std::memory_order_acquire);
        size_t Tail = DequeuePos;
        size_t Avail = Published - Tail;
        if (Avail == 0) return 0;
        size_t ToRead = (Avail < MaxCount) ? Avail : MaxCount;
        for (size_t i = 0; i < ToRead; ++i)
        {
            size_t Idx = (Tail + i) & CapacityMask;
            OutBuffer[i] = Slots[Idx].Storage;
        }
        DequeuePos = Tail + ToRead;
        return ToRead;
    }

    size_t Capacity() const { return CapacityMask + 1; }

private:
    struct Slot
    {
        T Storage;
    };

    Slot*                 Slots;
    size_t                SlotCount;
    size_t                CapacityMask;
    std::atomic<size_t>   EnqueuePos{0};     // 已抢占的上界(含未发布)
    std::atomic<size_t>   PublishedHead{0};  // 已发布的上界
    size_t                DequeuePos{0};     // 单消费者进度
};
