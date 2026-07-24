#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogCategory.h"
#include "Common/Runtime/Log/LogSinks.h"  // ELogSinkId + MakeSinkMask live here (Task 5)
#include <atomic>

// ELogSinkId / MakeSinkMask were defined here in Task 4 as a temporary home.
// They are lifted into LogSinks.h (Task 5) so the enum lives next to the
// ILogSink interface that consumes its bit values. Bit values 0..4 are STABLE:
// do not reorder.

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
