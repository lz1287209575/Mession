/**
 * @file LogCategories.cpp
 * @brief 全局日志分类定义 —— 用 DEFINE_LOG_CATEGORY 落实 LogCategories.h 中的 extern 声明。
 */
#include "Common/Runtime/Log/LogCategories.h"
#include "Common/Runtime/Log/LogRegistry.h"
DEFINE_LOG_CATEGORY(LogCore);
DEFINE_LOG_CATEGORY(LogNet);
DEFINE_LOG_CATEGORY(LogDb);
DEFINE_LOG_CATEGORY(LogRpc);
DEFINE_LOG_CATEGORY(LogAuth);
DEFINE_LOG_CATEGORY(LogScene);
DEFINE_LOG_CATEGORY(LogLua);
