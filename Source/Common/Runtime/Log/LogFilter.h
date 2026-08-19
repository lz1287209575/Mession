#pragma once
#include "Common/Runtime/Log/LogCategory.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogMetrics.h"
#include "Common/Runtime/MLib.h"

// Per-record filter (spec §8.1): drops records that are suppressed or whose
// level is below the category's RuntimeLevel. Returns true if the record
// should be enqueued, false if it was dropped.
//
// Dropped records increment TotalSuppressed (global) and the per-category
// SuppressedByCategory[Cat.Id] counter so the count is observable per
// category via MLogMetrics::Snapshot(). MLogRouter's SinkMask==0 path is
// a distinct event handled in the Router (see LogRouter.cpp), so this
// does not double-count.
class MLogFilter {
    public:
    static bool ShouldLog(const SLogCategory* Category, ELogLevel Level) {
        if (Category == nullptr)
            return false;
        if (Category->bSuppressed.load(std::memory_order_relaxed)) {
            MLogMetrics::IncSuppressed();
            MLogMetrics::IncSuppressedByCategory(Category->Id);
            return false;
        }
        const ELogLevel Runtime = Category->RuntimeLevel.load(std::memory_order_relaxed);
        if (static_cast<int>(Level) < static_cast<int>(Runtime)) {
            MLogMetrics::IncSuppressed();
            MLogMetrics::IncSuppressedByCategory(Category->Id);
            return false;
        }
        return true;
    }
};
