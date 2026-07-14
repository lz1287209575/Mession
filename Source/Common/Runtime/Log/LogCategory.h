#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"
#include <atomic>
#include <cstring>

// SLogCategory - single registered log channel (level, suppression, drop counter).
// Registered globally via MLogRegistry; lifetime is owned by the registry.
struct SLogCategory
{
    const char*            Name          = nullptr;
    uint16                 Id            = 0;
    ELogLevel              DefaultLevel  = ELogLevel::Info;
    std::atomic<ELogLevel> RuntimeLevel{ ELogLevel::Info };
    std::atomic<bool>      bSuppressed{  false };
    std::atomic<uint64>    DropCount{    0 };
};

// DECLARE_LOG_CATEGORY_EXTERN / DEFINE_LOG_CATEGORY pair.
//
// Note on the global-instance type: the original brief wrote
// `struct SLogCategoryInstance` here as the global instance type. SLogCategory
// contains std::atomic members, which makes it non-copyable, so the brief's
// `SLogCategory Name = *MLogRegistry::Get().RegisterCategory(...)` pattern
// cannot compile. We resolve this by making the macro's global a pointer
// (`SLogCategory* Name`) into the registry, so there is exactly one SLogCategory
// per category and the std::atomic fields remain live. Callers continue to use
// `Name` exactly as they would a value (e.g. `LogCore.Name`, `LogCore.Id`,
// `LogCore.RuntimeLevel`).
//
// DECLARE goes in a header (e.g. LogCategories.h), DEFINE in the matching .cpp.
#define DECLARE_LOG_CATEGORY_EXTERN(Name) extern SLogCategory* Name;

#define DEFINE_LOG_CATEGORY(Name) \
    SLogCategory* Name = MLogRegistry::Get().RegisterCategory(#Name, ELogLevel::Info);
