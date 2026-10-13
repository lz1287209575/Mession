/**
 * @file UdpSink.h
 * @brief MUdpSink —— 新管线(规范 §5.8)UDP sink,以 JSON Lines 序列化
 * 并以数据报发送。
 *
 * @detail 每条记录一行('\n' 结尾)。一批若会超过路径 MTU,则拆成多个
 * 包(每包一条或多条完整记录,绝不拆半条)。
 *
 * Target 是点分十进制 "ip:port" 串(如 "127.0.0.1:9999")。Open()
 * 解析一次并缓存 sockaddr_in;Close() 关闭 socket。WriteBatch() 短暂
 * 持有 WriteMutex 串行化 sockaddr 访问,随后在锁外 sendto();调用
 * WriteBatch 的生产方线程是唯一碰 socket 的线程,故无需多生产者同步。
 *
 * Flush() 是空操作:UDP 没有 flush 语义,逐条 sendto() 已立即推给内核。
 */
#pragma once

#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/MLib.h"

#include <atomic>
#include <mutex>


class MUdpSink : public ILogSink {
    public:
    MUdpSink();
    ~MUdpSink() override;

    // 新管线
    bool      Open() override;
    void      Close() override;
    void      WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer) override;
    void      Flush() override;
    ELogLevel MinLevel() const override {
        return MinLevelValue;
    }
    const char* Name() const override {
        return "udp";
    }

    // 配置。必须在 Open() 之前设置。
    MString Target;                       // 形如 "ip:port"
    int     MaxDatagramBytes = 63 * 1024; // 安全上限;典型 MTU 约 1400

    void SetMinLevel(ELogLevel Level) {
        MinLevelValue = Level;
    }

    private:
    ELogLevel  MinLevelValue = ELogLevel::Trace;
    int        SockFd        = -1;
    bool       bOpen         = false;
    std::mutex WriteMutex;
};