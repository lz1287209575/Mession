/**
 * @file main.cpp
 * @brief LogTest 入口 —— 运行本测试可执行文件内所有 TEST_CASE。
 *
 * 实现说明:TestHarness.h 的计数器是 `static`(每个 TU 各持一份内部链接)。
 * 为了让本 TU 的 RUN_TESTS() 看到测试 TU 递增的同一份计数,这里直接
 * #include 测试源文件,把测试 TU 塌缩进 main.cpp,从而共用内部链接的
 * static 变量。(替代方案是改成 extern + 单独的 TestGlobals.cpp 定义;
 * 暂不采用,以保持与任务书原文一致。)
 *
 * 下面的 include 列表已随任务推进补齐 —— 文件头旧注释里「Task 1 只链接
 * TestMpscRingBuffer.cpp」的说法已不成立。
 */
#include "Common/Runtime/Log/Tests/TestLogContext.cpp"
#include "Common/Runtime/Log/Tests/TestLogMetrics.cpp"
#include "Common/Runtime/Log/Tests/TestLogPerf.cpp"
#include "Common/Runtime/Log/Tests/TestLogRegistry.cpp"
#include "Common/Runtime/Log/Tests/TestLogRouter.cpp"
#include "Common/Runtime/Log/Tests/TestLogSinks.cpp"
#include "Common/Runtime/Log/Tests/TestMpscRingBuffer.cpp"
#include "Common/Runtime/Log/Tests/TestHarness.h"

int main() {
    std::printf("Running LogTest (Task 1+2+3+4+5+10)...\n");

    std::printf("[ MpscRingBuffer_Basic ]\n");
    Test_MpscRingBuffer_Basic();

    std::printf("[ MpscRingBuffer_MultiProducer ]\n");
    Test_MpscRingBuffer_MultiProducer();

    std::printf("[ MpscRingBuffer_FullReturnsFalse ]\n");
    Test_MpscRingBuffer_FullReturnsFalse();

    std::printf("[ LogContext_SetAndUnset ]\n");
    Test_LogContext_SetAndUnset();

    std::printf("[ LogContext_NestedScope ]\n");
    Test_LogContext_NestedScope();

    std::printf("[ LogRegistry_RegisterAndLookup ]\n");
    Test_LogRegistry_RegisterAndLookup();

    std::printf("[ LogRegistry_GetById ]\n");
    Test_LogRegistry_GetById();

    std::printf("[ LogRegistry_FindByNameAndCount ]\n");
    Test_LogRegistry_FindByNameAndCount();

    std::printf("[ LogMetrics_CountersAccumulate ]\n");
    Test_LogMetrics_CountersAccumulate();

    std::printf("[ LogMetrics_SnapshotIsConsistent ]\n");
    Test_LogMetrics_SnapshotIsConsistent();

    std::printf("[ LogRouter_DefaultAllSinks ]\n");
    Test_LogRouter_DefaultAllSinks();

    std::printf("[ LogRouter_SetRuleChangesMask ]\n");
    Test_LogRouter_SetRuleChangesMask();

    std::printf("[ LogRouter_MinLevelFiltersLowerLevels ]\n");
    Test_LogRouter_MinLevelFiltersLowerLevels();

    std::printf("[ LogSinks_ConsoleWriteBatchToStdout ]\n");
    Test_LogSinks_ConsoleWriteBatchToStdout();

    std::printf("[ LogSinks_RollingFileWritesJsonLines ]\n");
    Test_LogSinks_RollingFileWritesJsonLines();

    std::printf("[ LogSinks_MSpanSmokeTest ]\n");
    Test_LogSinks_MSpanSmokeTest();

    std::printf("[ LogPerf_EnqueueP99UnderOneUs ]\n");
    Test_LogPerf_EnqueueP99UnderOneUs();

    std::printf("[ LogPerf_ConcurrentThroughputMeetsOneMillion ]\n");
    Test_LogPerf_ConcurrentThroughputMeetsOneMillion();

    std::printf("[ LogPerf_BackpressureDropsProtectErrorLevel ]\n");
    Test_LogPerf_BackpressureDropsProtectErrorLevel();

    RUN_TESTS();
}
