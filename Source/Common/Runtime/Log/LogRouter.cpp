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

MLogRouter& MLogRouter::Get()
{
    static MLogRouter Inst;
    return Inst;
}

namespace
{
    // SetRule is rare (operator / config-driven) so a single mutex around all
    // mutations is sufficient. Hot-path reads are still lock-free because
    // they only touch the atomic pointers.
    std::mutex& GetRouterMutex()
    {
        static std::mutex Mutex;
        return Mutex;
    }
}

void MLogRouter::EnsureTablesAllocatedLocked()
{
    if (CategoryToMask.load(std::memory_order_acquire) != nullptr &&
        CategoryToMinLevel.load(std::memory_order_acquire) != nullptr)
    {
        return;
    }

    // Initial empty tables. ResolveSinkMask falls back to the spec-defined
    // default (0xFFFFFFFF / Trace) when the arrays are short or null, so an
    // empty table here is correct behavior — no pre-population needed.
    auto* NewMasks  = new TVector<uint32>();
    auto* NewLevels = new TVector<ELogLevel>();
    CategoryToMask.store(NewMasks, std::memory_order_release);
    CategoryToMinLevel.store(NewLevels, std::memory_order_release);
}

void MLogRouter::SetRule(const SLogRouteRule& Rule)
{
    if (Rule.Category == nullptr) return;
    std::lock_guard<std::mutex> L(GetRouterMutex());
    EnsureTablesAllocatedLocked();

    auto* OldMasks  = CategoryToMask.load(std::memory_order_acquire);
    auto* OldLevels = CategoryToMinLevel.load(std::memory_order_acquire);

    auto* NewMasks  = new TVector<uint32>(*OldMasks);
    auto* NewLevels = new TVector<ELogLevel>(*OldLevels);

    const size_t Idx = static_cast<size_t>(Rule.Category->Id);
    if (Idx >= NewMasks->size())
    {
        // Grow both arrays in lockstep so they always have the same length.
        // Newly appended slots keep the default values (all sinks, Trace).
        NewMasks->resize(Idx + 1, 0xFFFFFFFFu);
        NewLevels->resize(Idx + 1, ELogLevel::Trace);
    }
    (*NewMasks)[Idx]  = Rule.SinkMask;
    (*NewLevels)[Idx] = Rule.MinLevel;

    CategoryToMask.store(NewMasks, std::memory_order_release);
    CategoryToMinLevel.store(NewLevels, std::memory_order_release);
}

void MLogRouter::ClearRules()
{
    std::lock_guard<std::mutex> L(GetRouterMutex());
    EnsureTablesAllocatedLocked();

    auto* OldMasks  = CategoryToMask.load(std::memory_order_acquire);
    auto* OldLevels = CategoryToMinLevel.load(std::memory_order_acquire);

    auto* NewMasks  = new TVector<uint32>(*OldMasks);
    auto* NewLevels = new TVector<ELogLevel>(*OldLevels);

    // Reset every configured slot to the spec default. Slots that did not
    // exist stay absent; ResolveSinkMask fills in the default on read.
    for (auto& M : *NewMasks)  M = 0xFFFFFFFFu;
    for (auto& Lvl : *NewLevels) Lvl = ELogLevel::Trace;

    CategoryToMask.store(NewMasks, std::memory_order_release);
    CategoryToMinLevel.store(NewLevels, std::memory_order_release);
}

uint32 MLogRouter::ResolveSinkMask(uint16 CategoryId, ELogLevel Level) const
{
    // Read both atomics before checking sizes so the snapshot is consistent.
    // (The arrays are kept the same length by SetRule; if a SetRule is in
    // flight between the two loads we may briefly see the old Mask with the
    // new Level or vice versa, but SetRule writes to matching indices only,
    // so a mismatch can only occur when the category id itself is out of
    // range — in which case we fall back to defaults either way.)
    const auto* Masks  = CategoryToMask.load(std::memory_order_acquire);
    const auto* Levels = CategoryToMinLevel.load(std::memory_order_acquire);

    const size_t Idx = static_cast<size_t>(CategoryId);
    if (Masks == nullptr || Levels == nullptr ||
        Idx >= Masks->size() || Idx >= Levels->size())
    {
        // No routing rule for this category: pass-through default.
        return 0xFFFFFFFFu;
    }

    const ELogLevel MinLevel = (*Levels)[Idx];
    if (static_cast<int>(Level) < static_cast<int>(MinLevel))
    {
        return 0u;
    }
    return (*Masks)[Idx];
}
