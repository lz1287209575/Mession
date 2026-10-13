/**
 * @file TestLogSinks.cpp
 * @brief ILogSink 各实现(sink 契约、JSON Lines 落盘、TSpan 接口)测试。
 */
#include "Common/Runtime/Json.h"
#include "Common/Runtime/Log/ConsoleSink.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogRegistry.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/Log/LogStringTable.h"
#include "Common/Runtime/Log/RollingFileSink.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"

#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace {
    // 最小的记录构造。载荷内嵌(短消息)消息;FileStringId / FuncStringId /
    // CategoryId 都可以为 0,因为此处被测的 sink 对未驻留的 id 会回退成
    // 空串,而 Inline 分支把消息直接存在记录里。
    SLogRecord MakeRecord(ELogLevel Level, const char* Message, size_t Len) {
        SLogRecord R{};
        R.TimestampNs       = 1234567890ull;
        R.ThreadId          = 42;
        R.CategoryId        = 0;
        R.Level             = static_cast<uint8>(Level);
        R.Flags             = 0;
        R.FileStringId      = 0;
        R.Line              = 0;
        R.FuncStringId      = 0;
        R.SinkMask          = 0xFFFFFFFFu;
        R.ContextSnapshotId = 0;
        if (Len > sizeof(R.Payload.Inline.Data)) {
            Len = sizeof(R.Payload.Inline.Data);
        }
        std::memcpy(R.Payload.Inline.Data, Message, Len);
        if (Len < sizeof(R.Payload.Inline.Data)) {
            R.Payload.Inline.Data[Len] = '\0';
        }
        R.Flags = static_cast<uint8>(Len < std::strlen(Message) ? 1u : 0u);
        return R;
    }
} // namespace

TEST_CASE(LogSinks_ConsoleWriteBatchToStdout) {
    MLogStringTable::Init();
    MLogRegistry::Get().RegisterCategory("TestSinksConsole", ELogLevel::Trace);

    // 建立新管线的 ConsoleSink(ConsoleSink.h 中声明)。这里不抓 stdout:
    // 目标是验证 Open/WriteBatch/Close/Name 这套管线端到端不崩。既有的
    // MConsoleSink(旧 logger 的 sink)由生产日志路径单独覆盖;两者并存,
    // 是因为新 ILogSink 接口为兼容 MLogger 保留了旧的
    // Write(ELogLevel, MString&) 虚函数,同时定义了新的批量契约。
    // 本用例只走新契约。

    // 验证公开的编译期接口形态正确。
    MConsoleSink Sink;
    EXPECT_EQ(Sink.Name(), "console");
    EXPECT_TRUE(Sink.Open());
    EXPECT_EQ(static_cast<int>(Sink.MinLevel()), static_cast<int>(ELogLevel::Trace));

    SLogRecord Records[3];
    Records[0] = MakeRecord(ELogLevel::Info, "first", 5);
    Records[1] = MakeRecord(ELogLevel::Warn, "second", 6);
    Records[2] = MakeRecord(ELogLevel::Error, "third", 5);

    char                    Buffer[4096] = {};
    TSpanMutable<char>      OutBuf(Buffer, sizeof(Buffer));
    TSpan<const SLogRecord> Batch(Records, 3);

    Sink.WriteBatch(Batch, OutBuf);
    Sink.Flush();
    Sink.Close();

    EXPECT_TRUE(true); // 能跑到这里而不崩,本身就是断言
}

TEST_CASE(LogSinks_RollingFileWritesJsonLines) {
    MLogStringTable::Init();
    MLogRegistry::Get().RegisterCategory("TestSinksRolling", ELogLevel::Trace);

    constexpr const char* kPath = "/tmp/test-mession-log.jsonl";
    std::remove(kPath);

    MRollingFileSink Sink;
    Sink.ServiceName = "TestService";
    Sink.FilePath    = kPath;
    EXPECT_TRUE(Sink.Open());
    EXPECT_EQ(std::strcmp(Sink.Name(), "file"), 0);

    SLogRecord Records[3];
    Records[0] = MakeRecord(ELogLevel::Info, "alpha", 5);
    Records[1] = MakeRecord(ELogLevel::Warn, "beta", 4);
    Records[2] = MakeRecord(ELogLevel::Error, "gamma", 5);

    char                    Buffer[4096] = {};
    TSpanMutable<char>      OutBuf(Buffer, sizeof(Buffer));
    TSpan<const SLogRecord> Batch(Records, 3);

    Sink.WriteBatch(Batch, OutBuf);
    Sink.Flush();
    Sink.Close();

    // 读回内容:统计非空行,并逐行校验可解析为 JSON。
    std::ifstream In(kPath);
    EXPECT_TRUE(In.is_open());
    int         Lines = 0;
    std::string Line;
    while (std::getline(In, Line)) {
        if (Line.empty())
            continue;
        ++Lines;
        MJsonValue V;
        MString    Err;
        const bool Parsed = MJsonReader::Parse(MString(Line), V, Err);
        if (!Parsed) {
            std::printf("  PARSE ERR: %s\n", Err.c_str());
        }
        EXPECT_TRUE(Parsed);
        EXPECT_TRUE(V.IsObject());
    }
    EXPECT_EQ(Lines, 3);

    std::remove(kPath);
}

TEST_CASE(LogSinks_MSpanSmokeTest) {
    // 守护 TSpan / TSpanMutable 所依赖的 cpp17 MSpan<T> 接口不被改歪:
    // 构造、data()、size()、empty()、range-for。任何一项失效,所有构造
    // TSpan/TSpanMutable 的 Log TU 都会先编译失败 —— 但显式断言能让意图
    // 一目了然,也能抓住更隐蔽的回归。

    int              Ints[] = {10, 20, 30, 40};
    TSpan<const int> View(Ints, 4);
    EXPECT_EQ(View.data(), Ints);
    EXPECT_EQ(View.size(), static_cast<size_t>(4));
    EXPECT_TRUE(!View.empty());

    int Sum = 0;
    for (const int& V : View)
        Sum += V;
    EXPECT_EQ(Sum, 100);

    TSpan<const int> Empty;
    EXPECT_EQ(Empty.data(), nullptr);
    EXPECT_EQ(Empty.size(), static_cast<size_t>(0));
    EXPECT_TRUE(Empty.empty());

    char               Buf[16] = {};
    TSpanMutable<char> Scratch(Buf, sizeof(Buf));
    EXPECT_EQ(Scratch.data(), Buf);
    EXPECT_EQ(Scratch.size(), static_cast<size_t>(16));
    EXPECT_TRUE(!Scratch.empty());
}
