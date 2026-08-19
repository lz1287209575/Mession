#pragma once
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/MLib.h"

// Note: brief's static_assert(sizeof(ELogLevel) <= sizeof(uint8)) is dropped.
// ELogLevel is `enum class : int` in this codebase (4B), and changing it to :uint8
// would alter ABI/int promotion for the entire logger stack (Logger.cpp, sinks).
// LogRecord stores the level as uint8 and the producer casts ELogLevel -> uint8
// at the call site, which is safe for the 6-value range.

// SLogRecord: 64B fixed-size POD log entry.
// Layout:
//   Header         — 32B  (TimestampNs, ThreadId, CategoryId, Level, Flags, Reserved)
//   Source location— 8B   (FileStringId, Line, FuncStringId)
//   Routing+Context— 8B   (SinkMask, ContextSnapshotId)
//   Payload        — 128B (union of Inline[128] / Overflow{Offset,Length,Capacity,Reserved})
struct SLogRecord {
    // Header — 32B (timestamps, thread, category, level, flags, 16B reserved for future use)
    uint64 TimestampNs; // steady_clock 纳秒
    uint32 ThreadId;
    uint16 CategoryId;
    uint8  Level;              // ELogLevel 强转
    uint8  Flags;              // bit0: MessageOverflowed, bit1: HasSourceLocation
    uint8  HeaderReserved[16]; // 占位 / 16 字节对齐 + 留作后续字段(TraceId 等)

    // Source location — 8B
    uint32 FileStringId;
    uint16 Line;
    uint16 FuncStringId;

    // Routing + Context — 8B
    uint32 SinkMask;
    uint32 ContextSnapshotId;

    // Payload — 128B
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
