#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogCategory.h"
#include "Common/Runtime/Log/LogMetrics.h"

// Per-record filter (spec §8.1): drops records that are suppressed or whose
// level is below the category's RuntimeLevel. Returns true if the record
// should be enqueued, false if it was dropped.
//
// Dropped records increment TotalSuppressed so the count is observable via
// MLogMetrics::Snapshot(). We do not increment per-category counters here
// because MLogRouter also contributes to the same total; a single increment
// keeps the accounting simple and consistent.
class MLogFilter
{
public:
    static bool ShouldLog(const SLogCategory* Category, ELogLevel Level)
    {
        if (Category == nullptr) return false;
        if (Category->bSuppressed.load(std::memory_order_relaxed))
        {
            MLogMetrics::IncSuppressed();
            return false;
        }
        const ELogLevel Runtime = Category->RuntimeLevel.load(std::memory_order_relaxed);
        if (static_cast<int>(Level) < static_cast<int>(Runtime))
        {
            MLogMetrics::IncSuppressed();
            return false;
        }
        return true;
    }
};
