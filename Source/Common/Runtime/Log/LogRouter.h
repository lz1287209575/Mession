#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogCategory.h"
#include <atomic>

// Sink identifier enum used as bit indices into SinkMask.
//
// Defined here for Task 4; Task 5 (LogSinks.h consolidation) will lift this
// enum to its own header and may add additional sink IDs. Bit values must
// stay stable so routing rules written before Task 5 keep working.
enum class ELogSinkId : uint32
{
    Console  = 0,   // 1<<0
    File     = 1,   // 1<<1
    Udp      = 2,   // 1<<2
    Tcp      = 3,   // 1<<3
    Coredump = 4,   // 1<<4
};

struct SLogRouteRule
{
    const SLogCategory* Category = nullptr;
    uint32              SinkMask = 0xFFFFFFFFu;
    ELogLevel           MinLevel = ELogLevel::Trace;
};

// Category -> (SinkMask, MinLevel) routing table.
//
// Reads in the hot path (ResolveSinkMask) are lock-free via two
// std::atomic<T*> pointers holding the current generation of the table.
// Writes (SetRule / ClearRules) copy the current arrays, mutate the copy,
// then publish the new pointers atomically. Old generations remain valid for
// in-flight readers and are reclaimed by a generation counter (no per-call
// delete — see .cpp for the policy).
class MLogRouter
{
public:
    static MLogRouter& Get();

    // Apply or replace the routing rule for a single category.
    void SetRule(const SLogRouteRule& Rule);

    // Drop all custom routing; everything falls back to default (all sinks,
    // no level filtering).
    void ClearRules();

    // Hot-path read. Returns the bitmask of sinks that should receive records
    // of `Level` for `CategoryId`, or 0 if the level is below MinLevel or no
    // sinks are configured for the category.
    uint32 ResolveSinkMask(uint16 CategoryId, ELogLevel Level) const;

private:
    MLogRouter() = default;

    // Lazily allocate the initial empty tables. Called from SetRule under the
    // writer mutex on first use; no need for atomic-CAS there. Kept as a
    // private member so the writer code can mutate the member atomics.
    void EnsureTablesAllocatedLocked();

    // Each pointer is replaced as a unit; readers always see a consistent
    // pair via the same generation (see .cpp for the bump sequence).
    std::atomic<TVector<uint32>*>    CategoryToMask{ nullptr };
    std::atomic<TVector<ELogLevel>*> CategoryToMinLevel{ nullptr };
};
