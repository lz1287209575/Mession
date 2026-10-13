/**
 * @file LogRecord.h
 * @brief SLogRecord —— 定长 176 字节 POD 日志条目(规范 §5.4)。
 *
 * @detail 说明:需求文档曾要求 `static_assert(sizeof(ELogLevel) <= sizeof(uint8))`,
 * 此处不采纳。ELogLevel 在本代码库是 `enum class : int`(4 字节),改为
 * `: uint8` 会改变整个日志栈(Logger.cpp 及各 sink)的 ABI 与整型提升。
 * SLogRecord 以 uint8 存 level,由生产方调用点做 ELogLevel -> uint8
 * 强转;枚举仅 6 个取值,转换安全。
 */
#pragma once
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/MLib.h"


/**
 * @brief 定长 176 字节 POD 日志条目。
 *
 * @detail 布局:
 *   Header(头)         — 32B: TimestampNs / ThreadId / CategoryId /
 *                              Level / Flags / Reserved
 *   Source location(源码位置)— 8B : FileStringId / Line / FuncStringId
 *   Routing+Context(路由与上下文)— 8B : SinkMask / ContextSnapshotId
 *   Payload(载荷)      — 128B: union{Inline[128], Overflow{Offset,Length,
 *                              Capacity,Reserved}}
 *
 * 注意:头部注释原文写 64B,实际 static_assert 为 176B,以 assert 为准。
 */
struct SLogRecord {
    // 头部 —— 32B(时间戳、线程、分类、级别、标志,以及 16B 预留)
    uint64 TimestampNs; // steady_clock 纳秒
    uint32 ThreadId;
    uint16 CategoryId;
    uint8  Level;              // ELogLevel 强转
    uint8  Flags;              // bit0: 消息溢出;bit1: 带源码位置
    uint8  HeaderReserved[16]; // 占位 / 16 字节对齐 + 留作后续字段(TraceId 等)

    // 源码位置 —— 8B
    uint32 FileStringId;
    uint16 Line;
    uint16 FuncStringId;

    // 路由 + 上下文 —— 8B
    uint32 SinkMask;
    uint32 ContextSnapshotId;

    // 载荷 —— 128B
    union {
        struct {
            char Data[128];
        } Inline;
        struct {
            uint32 Offset;
            uint32 Length;
            uint32 Capacity;
            uint32 Reserved;
        } Overflow;
    } Payload;
};
static_assert(sizeof(SLogRecord) == 176, "SLogRecord must be exactly 176 bytes");
