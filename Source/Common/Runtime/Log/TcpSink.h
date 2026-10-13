#pragma once

#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/MLib.h"

#include <atomic>
#include <mutex>

/**
 * @file TcpSink.h
 * @brief MTcpSink —— 新管线(规范 §5.8)TCP sink,维持到 Target 的单条
 * TCP 连接并流式下发 JSON Lines。
 *
 * @detail 连接丢失时(send() 返回 ECONNRESET 或 EPIPE),sink 进入
 * 5 秒退避并尝试一次重连;断开窗口内发出的记录经 MLogMetrics 记为丢弃。
 *
 * WriteBatch() 短暂持有 WriteMutex 保护连接态迁移。调用 WriteBatch
 * 的生产方线程独占有在途 send() —— 对同一 fd 而言 TCP 的并发调用
 * 并非线程安全。
 */

class MTcpSink : public ILogSink {
    public:
    MTcpSink();
    ~MTcpSink() override;

    // 新管线
    bool      Open() override;
    void      Close() override;
    void      WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer) override;
    void      Flush() override;
    ELogLevel MinLevel() const override {
        return MinLevelValue;
    }
    const char* Name() const override {
        return "tcp";
    }

    // 配置。必须在 Open() 之前设置。
    MString Target; // "ip:port"
    int     ReconnectBackoffMs = 5000;

    void SetMinLevel(ELogLevel Level) {
        MinLevelValue = Level;
    }

    private:
    ELogLevel MinLevelValue = ELogLevel::Trace;
    int       SockFd        = -1;
    bool      bOpen         = false;

    // 记录下一次允许重连的时刻。访问须持有 WriteMutex。
    long long NextReconnectAtMs = 0;

    std::mutex WriteMutex;

    // 从 Ptr 起阻塞发送 Bytes 字节。出错时断开连接(CloseLocked),
    // 使下一次 WriteBatch 触发重连。
    void SendLocked(const char* Ptr, size_t Bytes);

    // 关闭 socket。调用方须持有 WriteMutex。
    void CloseLocked();
};