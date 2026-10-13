/**
 * @file LogRouter.cpp
 * @brief MLogRouter —— 分类 → (SinkMask, MinLevel) 路由表。
 *
 * 存储:两个并行数组(掩码、最低级别),各自通过 std::atomic<TVector<T>*>
 * 发布。读者(ResolveSinkMask)对两个指针各做一次原子加载后索引;写者
 * (SetRule / ClearRules)持互斥量,拷贝当前数组、改副本、再原子换指针。
 *
 * 旧世代数组不回收:可能仍有读者持有旧指针,而在确定性时点回收需要一次
 * 全局屏障 —— 热路径不值得付这个代价。日志路由的世代数在进程生命周期内
 * 小而有界,故暂时容忍。若内存压力出现,把裸指针换成 shared_ptr 让世代
 * 自然衰减即可,对外 API 不变。
 *
 * 两数组长度必须始终一致:SetRule 同步扩容两者,使得在最后一次写入之后
 * 才注册的分类,在下一次读取时同样能拿到合理的默认槽位。
 */
#include "Common/Runtime/Log/LogRouter.h"
#include <mutex>

// MLogRouter — category -> (SinkMask, MinLevel) routing table.
//
// Storage: two parallel arrays (one for masks, one for min levels) published
// via std::atomic<TVector<T>*>. Readers (ResolveSinkMask) do an atomic load
// of each pointer, then index. Writers (SetRule / ClearRules) take a mutex,
// copy the current arrays, mutate the copies, and atomically swap both
// pointers. Old generations live until every reader that captured them has
// finished — we never free them here; for typical log-routing workloads the
// number of distinct generations over a process lifetime is small and
// bounded, and freeing on a quiescent point would require a global barrier
// that we don't need to pay for on the hot path. If memory pressure becomes
// a concern, replace the raw pointers with shared_ptr and let the generation
// decay naturally; the public API stays the same.
//
// Both arrays must always have the same size; SetRule extends them in lockstep
// so a category registered after the last write still gets a sensible default
// slot on the next read.

MLogRouter& MLogRouter::Get() {
    static MLogRouter Inst;
    return Inst;
}

namespace {
    // SetRule 调用很少(运维/配置驱动),因此所有修改共用一把互斥量即可。
    // 热路径读仍然无锁 —— 只碰那两个原子指针。
    std::mutex& GetRouterMutex() {
        static std::mutex Mutex;
        return Mutex;
    }
} // namespace

void MLogRouter::EnsureTablesAllocatedLocked() {
    if (CategoryToMask.load(std::memory_order_acquire) != nullptr && CategoryToMinLevel.load(std::memory_order_acquire) != nullptr) {
        return;
    }

    // 初始为空表。当数组为空或长度不足时,ResolveSinkMask 会回落到规范
    // 默认值(0xFFFFFFFF / Trace),所以空表就是正确行为,无需预填。
    auto* NewMasks  = new TVector<uint32>();
    auto* NewLevels = new TVector<ELogLevel>();
    CategoryToMask.store(NewMasks, std::memory_order_release);
    CategoryToMinLevel.store(NewLevels, std::memory_order_release);
}

void MLogRouter::SetRule(const SLogRouteRule& Rule) {
    if (Rule.Category == nullptr)
        return;
    std::lock_guard<std::mutex> L(GetRouterMutex());
    EnsureTablesAllocatedLocked();

    auto* OldMasks  = CategoryToMask.load(std::memory_order_acquire);
    auto* OldLevels = CategoryToMinLevel.load(std::memory_order_acquire);

    auto* NewMasks  = new TVector<uint32>(*OldMasks);
    auto* NewLevels = new TVector<ELogLevel>(*OldLevels);

    const size_t Idx = static_cast<size_t>(Rule.Category->Id);
    if (Idx >= NewMasks->size()) {
        // 两个数组同步扩容,始终保持等长。新追加的槽位保持默认值
        // (全部 sink、Trace 级别)。
        NewMasks->resize(Idx + 1, 0xFFFFFFFFu);
        NewLevels->resize(Idx + 1, ELogLevel::Trace);
    }
    (*NewMasks)[Idx]  = Rule.SinkMask;
    (*NewLevels)[Idx] = Rule.MinLevel;

    CategoryToMask.store(NewMasks, std::memory_order_release);
    CategoryToMinLevel.store(NewLevels, std::memory_order_release);
}

void MLogRouter::ClearRules() {
    std::lock_guard<std::mutex> L(GetRouterMutex());
    EnsureTablesAllocatedLocked();

    auto* OldMasks  = CategoryToMask.load(std::memory_order_acquire);
    auto* OldLevels = CategoryToMinLevel.load(std::memory_order_acquire);

    auto* NewMasks  = new TVector<uint32>(*OldMasks);
    auto* NewLevels = new TVector<ELogLevel>(*OldLevels);

    // 把所有已配置槽位重置为规范默认值。本就不存在的槽位保持缺席,
    // 由 ResolveSinkMask 在读取时补默认值。
    for (auto& M : *NewMasks)
        M = 0xFFFFFFFFu;
    for (auto& Lvl : *NewLevels)
        Lvl = ELogLevel::Trace;

    CategoryToMask.store(NewMasks, std::memory_order_release);
    CategoryToMinLevel.store(NewLevels, std::memory_order_release);
}

uint32 MLogRouter::ResolveSinkMask(uint16 CategoryId, ELogLevel Level) const {
    // 先读两个原子量、再做长度检查,以保证快照一致。(SetRule 保证两数组
    // 等长;若一次 SetRule 恰好夹在两次 load 之间,可能短暂看到「旧掩码 +
    // 新级别」或反之 —— 但 SetRule 只写对应下标,因此只有分类 id 本身越界
    // 时才会不匹配,而那时两条路径都会回退到默认值。)
    const auto* Masks  = CategoryToMask.load(std::memory_order_acquire);
    const auto* Levels = CategoryToMinLevel.load(std::memory_order_acquire);

    const size_t Idx = static_cast<size_t>(CategoryId);
    if (Masks == nullptr || Levels == nullptr || Idx >= Masks->size() || Idx >= Levels->size()) {
        // 该分类没有路由规则:直接走透传默认值。
        return 0xFFFFFFFFu;
    }

    const ELogLevel MinLevel = (*Levels)[Idx];
    if (static_cast<int>(Level) < static_cast<int>(MinLevel)) {
        return 0u;
    }
    return (*Masks)[Idx];
}
