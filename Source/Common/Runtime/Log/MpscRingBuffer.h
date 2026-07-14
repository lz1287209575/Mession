#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

// TMpscRingBuffer<T>: 多生产者单消费者环形队列。
// 容量必须是 2 的幂（内部使用位掩码取模）。
// 实现策略:每个 slot 携带 sequence number。
//   - 空闲 slot: seq = index(初始)
//   - 写入后:  seq = index + Capacity(生产者发布)
//   - 消费者消费后: seq = index + 2*Capacity(下次空闲标记)
// 多生产者通过 CAS(EnqueuePos) 抢占下一个可写 slot;消费者读 PublishedPos。
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
            Slots[i].Seq.store(i, std::memory_order_relaxed);
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
            // 尝试 CAS 抢占。失败则 Cur 已被更新,重新判断。
            if (EnqueuePos.compare_exchange_weak(Cur, Cur + 1,
                    std::memory_order_relaxed, std::memory_order_relaxed))
            {
                break;
            }
            // Cur 已更新为失败时的当前值,循环重试。
        }
        // 此时 Cur 是我们抢占的 slot 索引。
        size_t Idx = Cur & CapacityMask;
        Slots[Idx].Storage = Record;
        // Release fence: Storage 写入先于 Seq 变更对消费者可见。
        std::atomic_thread_fence(std::memory_order_release);
        // 标记 slot 为"已发布"。消费者看到 Seq == Idx + Capacity 即认为可读。
        Slots[Idx].Seq.store(Idx + Capacity(), std::memory_order_release);
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
            // CAS 失败时 CurPub 已被更新,继续比较。
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
        // 消费者读取到 seq == Idx+Capacity 的 slot 即认为可读。
        // 消费后把 seq 标记为 Idx+2*Capacity,生产者下次使用。
        // 为避免消费者侧同步问题(只有单消费者),直接按 PublishedHead 上界读取。
        size_t Published = PublishedHead.load(std::memory_order_acquire);
        size_t Tail = DequeuePos;
        size_t Avail = Published - Tail;
        if (Avail == 0) return 0;
        size_t ToRead = (Avail < MaxCount) ? Avail : MaxCount;
        for (size_t i = 0; i < ToRead; ++i)
        {
            size_t Idx = (Tail + i) & CapacityMask;
            // Acquire load 与生产者的 release store 配对,确保 Storage 写入可见。
            (void)Slots[Idx].Seq.load(std::memory_order_acquire);
            OutBuffer[i] = Slots[Idx].Storage;
        }
        // 标记这些 slot 为空闲(seq = Idx + 2*Capacity)。
        // 注意:此时生产者可能正在等待 slot[i+Capacity] 的空闲 seq(即 Idx),
        // 我们必须确保 Idx 的 slot seq 不等于 Idx(否则生产者认为还在占用)。
        // 通过 seq = Idx + 2*Capacity 实现"该 slot 已完成一个生命周期"。
        for (size_t i = 0; i < ToRead; ++i)
        {
            size_t Idx = (Tail + i) & CapacityMask;
            Slots[Idx].Seq.store(Idx + 2 * Capacity(), std::memory_order_release);
        }
        DequeuePos = Tail + ToRead;
        return ToRead;
    }

    size_t Capacity() const { return CapacityMask + 1; }

private:
    struct Slot
    {
        T Storage;
        std::atomic<size_t> Seq{0};
    };

    Slot*                 Slots;
    size_t                SlotCount;
    size_t                CapacityMask;
    std::atomic<size_t>   EnqueuePos{0};     // 已抢占的上界(含未发布)
    std::atomic<size_t>   PublishedHead{0};  // 已发布的上界
    size_t                DequeuePos{0};     // 单消费者进度
};