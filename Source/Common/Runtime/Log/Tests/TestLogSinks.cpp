#include "Common/Runtime/Log/Tests/TestHarness.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/Log/LogStringTable.h"
#include "Common/Runtime/Log/LogRegistry.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/ConsoleSink.h"
#include "Common/Runtime/Log/RollingFileSink.h"
#include "Common/Runtime/Json.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>

namespace
{
    // Minimal record construction. The payload carries an inlined (≤16B)
    // message; FileStringId / FuncStringId / CategoryId may all be 0 because
    // the sinks we test fall back to "" for unresolved intern ids and the
    // Inline branch stores the message inline in the record itself.
    SLogRecord MakeRecord(ELogLevel Level, const char* Message, size_t Len)
    {
        SLogRecord R{};
        R.TimestampNs = 1234567890ull;
        R.ThreadId = 42;
        R.CategoryId = 0;
        R.Level = static_cast<uint8>(Level);
        R.Flags = 0;
        R.FileStringId = 0;
        R.Line = 0;
        R.FuncStringId = 0;
        R.SinkMask = 0xFFFFFFFFu;
        R.ContextSnapshotId = 0;
        if (Len > sizeof(R.Payload.Inline.Data))
        {
            Len = sizeof(R.Payload.Inline.Data);
        }
        std::memcpy(R.Payload.Inline.Data, Message, Len);
        if (Len < sizeof(R.Payload.Inline.Data))
        {
            R.Payload.Inline.Data[Len] = '\0';
        }
        R.Flags = static_cast<uint8>(Len < std::strlen(Message) ? 1u : 0u);
        return R;
    }
}

TEST_CASE(LogSinks_ConsoleWriteBatchToStdout)
{
    MLogStringTable::Init();
    MLogRegistry::Get().RegisterCategory("TestSinksConsole", ELogLevel::Trace);

    // Create the new-pipeline ConsoleSink (declared in ConsoleSink.h).
    // We don't capture stdout here; the goal is to verify that Open/WriteBatch/
    // Close/Flow-Name plumbing works end-to-end without crashing, since the
    // existing MConsoleSink (the legacy logger sink) is exercised separately
    // by the production logger path. The two coexist because the new
    // ILogSink interface keeps a legacy `Write(ELogLevel, MString&)` virtual
    // for compatibility with MLogger while defining the new batched contract.
    // We exercise only the new contract here.

    // Verify the public compile-time interface is shaped correctly.
    MConsoleSink Sink;
    EXPECT_EQ(Sink.Name(), "console");
    EXPECT_TRUE(Sink.Open());
    EXPECT_EQ(static_cast<int>(Sink.MinLevel()), static_cast<int>(ELogLevel::Trace));

    SLogRecord Records[3];
    Records[0] = MakeRecord(ELogLevel::Info,  "first",  5);
    Records[1] = MakeRecord(ELogLevel::Warn,  "second", 6);
    Records[2] = MakeRecord(ELogLevel::Error, "third",  5);

    char Buffer[4096] = {};
    TSpanMutable<char> OutBuf(Buffer, sizeof(Buffer));
    TSpan<const SLogRecord> Batch(Records, 3);

    Sink.WriteBatch(Batch, OutBuf);
    Sink.Flush();
    Sink.Close();

    EXPECT_TRUE(true);  // reaching here without crash is the assertion
}

TEST_CASE(LogSinks_RollingFileWritesJsonLines)
{
    MLogStringTable::Init();
    MLogRegistry::Get().RegisterCategory("TestSinksRolling", ELogLevel::Trace);

    constexpr const char* kPath = "/tmp/test-mession-log.jsonl";
    std::remove(kPath);

    MRollingFileSink Sink;
    Sink.ServiceName = "TestService";
    Sink.FilePath = kPath;
    EXPECT_TRUE(Sink.Open());
    EXPECT_EQ(std::strcmp(Sink.Name(), "file"), 0);

    SLogRecord Records[3];
    Records[0] = MakeRecord(ELogLevel::Info,  "alpha", 5);
    Records[1] = MakeRecord(ELogLevel::Warn,  "beta",  4);
    Records[2] = MakeRecord(ELogLevel::Error, "gamma", 5);

    char Buffer[4096] = {};
    TSpanMutable<char> OutBuf(Buffer, sizeof(Buffer));
    TSpan<const SLogRecord> Batch(Records, 3);

    Sink.WriteBatch(Batch, OutBuf);
    Sink.Flush();
    Sink.Close();

    // Read back, count non-empty lines, validate each is parseable JSON.
    std::ifstream In(kPath);
    EXPECT_TRUE(In.is_open());
    int Lines = 0;
    std::string Line;
    while (std::getline(In, Line))
    {
        if (Line.empty()) continue;
        ++Lines;
        MJsonValue V;
        MString Err;
        const bool Parsed = MJsonReader::Parse(MString(Line), V, Err);
        if (!Parsed)
        {
            std::printf("  PARSE ERR: %s\n", Err.c_str());
        }
        EXPECT_TRUE(Parsed);
        EXPECT_TRUE(V.IsObject());
    }
    EXPECT_EQ(Lines, 3);

    std::remove(kPath);
}

TEST_CASE(LogSinks_MSpanSmokeTest)
{
    // Guards the cpp17 MSpan<T> surface used by TSpan / TSpanMutable against
    // drift: ctor, data(), size(), empty(), range-for. If any of these break,
    // every Log TU that constructs a TSpan/TSpanMutable will fail to compile
    // long before this test runs — but having the explicit assertions makes
    // the intent obvious and catches subtler regressions.

    int Ints[] = {10, 20, 30, 40};
    TSpan<const int> View(Ints, 4);
    EXPECT_EQ(View.data(), Ints);
    EXPECT_EQ(View.size(), static_cast<size_t>(4));
    EXPECT_TRUE(!View.empty());

    int Sum = 0;
    for (const int& V : View) Sum += V;
    EXPECT_EQ(Sum, 100);

    TSpan<const int> Empty;
    EXPECT_EQ(Empty.data(), nullptr);
    EXPECT_EQ(Empty.size(), static_cast<size_t>(0));
    EXPECT_TRUE(Empty.empty());

    char Buf[16] = {};
    TSpanMutable<char> Scratch(Buf, sizeof(Buf));
    EXPECT_EQ(Scratch.data(), Buf);
    EXPECT_EQ(Scratch.size(), static_cast<size_t>(16));
    EXPECT_TRUE(!Scratch.empty());
}
