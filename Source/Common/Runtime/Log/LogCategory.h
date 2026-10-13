#pragma once
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/MLib.h"
#include <atomic>
#include <cstring>

/**
 * @file LogCategory.h
 * @brief SLogCategory 单个已注册日志通道,含级别 / 抑制 / 丢弃计数;
 * 以及分类声明宏。
 *
 * @detail SLogCategory 经 MLogRegistry 全局注册,生命周期由注册表持有。
 */
struct SLogCategory {
    const char*            Name         = nullptr;
    uint16                 Id           = 0;
    ELogLevel              DefaultLevel = ELogLevel::Info;
    std::atomic<ELogLevel> RuntimeLevel{ELogLevel::Info};
    std::atomic<bool>      bSuppressed{false};
    std::atomic<uint64>    DropCount{0};
};

/**
 * @brief DECLARE_LOG_CATEGORY_EXTERN / DEFINE_LOG_CATEGORY 声明 / 定义对。
 *
 * @detail 关于全局实例类型:需求原文把全局写成值类型 `SLogCategory Name`,
 * 但 SLogCategory 含 std::atomic 成员,不可拷贝 / 移动,
 * `SLogCategory Name = *...RegisterCategory(...)` 无法编译。这里改为
 * 全局指向注册表的指针(`SLogCategory* Name`),保证每个分类只有一份
 * SLogCategory 且 std::atomic 字段保持有效。
 *
 * 用法(调用方一律用箭头语法解引用):
 *   LogNet->Name
 *   LogNet->Id
 *   LogNet->RuntimeLevel
 *   MLog::Write(LogNet, ELogLevel::Info, Fmt, ...);   // 收指针
 *
 * DECLARE 放头文件(如 LogCategories.h),DEFINE 放同名 .cpp。
 */
#define DECLARE_LOG_CATEGORY_EXTERN(Name) extern SLogCategory* Name;

#define DEFINE_LOG_CATEGORY(Name) SLogCategory* Name = MLogRegistry::Get().RegisterCategory(#Name, ELogLevel::Info);
