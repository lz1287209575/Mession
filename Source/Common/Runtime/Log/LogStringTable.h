#pragma once

/**
 * @file LogStringTable.h
 * @brief MLogStringTable —— 字符串驻留表,把日志里的字符串
 * (文件名 / 函数名 / 消息模板)压成 uint32 id,节省日志记录空间。
 *
 * @detail Init() 在日志系统初始化时调用一次;Intern() 返回已有 id 或
 * 插入新串;Get() 按 id 取回 C 字符串(永久有效)。
 */

#include "Common/Runtime/MLib.h"

class MLogStringTable {
    public:
    static void        Init();
    static uint32      Intern(const char* Str);
    static const char* Get(uint32 Id);
    static size_t      Count();
};
