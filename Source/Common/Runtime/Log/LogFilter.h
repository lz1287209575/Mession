/**
 * @file LogFilter.h
 * @brief MLogFilter —— 单条日志入队前的分类级过滤(规范 §8.1)。
 *
 * @detail 丢弃两类记录:分类被抑制,或级别低于分类 RuntimeLevel。
 * 被丢记录同时累加全局 TotalSuppressed 与按分类的
 * SuppressedByCategory[Cat.Id],可通过 MLogMetrics::Snapshot() 观测。
 * 注意:MLogRouter 的 SinkMask==0 是另一类事件,在 Router 内部处理
 * (见 LogRouter.cpp),两者不会重复计数。
 */
#pragma once
#include "Common/Runtime/Log/LogCategory.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogMetrics.h"
#include "Common/Runtime/MLib.h"


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
