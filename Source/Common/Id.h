#pragma once 

#include "Common/MLib.h"


// 唯一ID生成器（线程安全）
class MUniqueIdGenerator
{
private:
    inline static std::atomic<uint64> CurrentId{0};

public:
    static uint64 Generate()
    {
        return CurrentId.fetch_add(1, std::memory_order_relaxed) + 1;
    }
};
