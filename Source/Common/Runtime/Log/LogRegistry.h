#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogCategory.h"
#include "Common/Runtime/Log/LogLevel.h"
#include <mutex>

class MLogRegistry
{
public:
    static MLogRegistry& Get();

    // Register a category by name. Idempotent: if a category with the same
    // name already exists, returns the existing entry without mutating it.
    // Otherwise appends a new entry whose Id equals the resulting size and
    // whose DefaultLevel / RuntimeLevel both equal DefaultLevel.
    SLogCategory* RegisterCategory(const char* Name, ELogLevel DefaultLevel);

    // Lookup
    const SLogCategory* GetById(uint16 Id) const;
    SLogCategory*       GetById(uint16 Id);
    const SLogCategory* FindByName(const MString& Name) const;

    size_t NumCategories() const { return Categories.size(); }

private:
    MLogRegistry() = default;

    // SLogCategory contains std::atomic members, which are non-copyable and
    // non-movable, so we cannot store them inline in TVector. The brief wrote
    // `TVector<SLogCategory>` but that cannot compile when the element type
    // has atomic members — std::vector requires the element to be at least
    // move-insertable. Instead we store unique_ptrs and hand out raw
    // SLogCategory* to callers; the pointers are stable across reallocation.
    TVector<TUniquePtr<SLogCategory>> Categories;
    // mutable so const FindByName can lock it
    mutable std::mutex Mutex;
};
