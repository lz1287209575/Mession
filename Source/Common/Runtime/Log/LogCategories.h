#pragma once

/**
 * @file LogCategories.h
 * @brief 项目内建日志分类的前置声明清单。
 *
 * @detail 每个分类在 LogCategories.cpp 中经 DEFINE_LOG_CATEGORY 定义,
 * 注册进 MLogRegistry;本头文件让各模块直接 `extern` 使用。
 */

#include "Common/Runtime/Log/LogCategory.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCore);
DECLARE_LOG_CATEGORY_EXTERN(LogNet);
DECLARE_LOG_CATEGORY_EXTERN(LogDb);
DECLARE_LOG_CATEGORY_EXTERN(LogRpc);
DECLARE_LOG_CATEGORY_EXTERN(LogAuth);
DECLARE_LOG_CATEGORY_EXTERN(LogScene);
DECLARE_LOG_CATEGORY_EXTERN(LogLua);
