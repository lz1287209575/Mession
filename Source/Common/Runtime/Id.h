/**
 * @file Id.h
 * @brief MUniqueIdGenerator —— 进程内自增的 64 位唯一 ID 生成器(线程安全)。
 *
 * 用 std::atomic<uint64> 的 fetch_add(relaxed) 实现,首个返回值为 1
 * (不发出 0,因此 0 可安全地当作「无效 ID」)。计数在进程重启后归零,所以
 * 该 ID 只在单进程生命周期内唯一 —— 不要持久化,也不要跨进程比较。
 */
#pragma once

#include "Common/Runtime/MLib.h"


// 唯一ID生成器（线程安全）
class MUniqueIdGenerator {
    private:
    inline static std::atomic<uint64> CurrentId{0};

    public:
    static uint64 Generate() {
        return CurrentId.fetch_add(1, std::memory_order_relaxed) + 1;
    }
};
