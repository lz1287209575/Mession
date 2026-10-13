/**
 * @file TestLogRouter.cpp
 * @brief MLogRouter 路由规则解析测试(默认值、掩码、最低级别、越界)。
 */
#include "Common/Runtime/Log/LogFilter.h"
#include "Common/Runtime/Log/LogRegistry.h"
#include "Common/Runtime/Log/LogRouter.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"

// Router 用例需要一张干净的规则表才能做确定性断言;每个用例入口都重置,
// 因此不受其他用例 SetRule/ClearRules 的影响。

TEST_CASE(LogRouter_DefaultAllSinks) {
    auto& Router = MLogRouter::Get();
    Router.ClearRules();

    // 未设置任何规则时,每个分类都应解析为默认行为「全部 sink 打开、不做
    // 级别过滤」:SinkMask == 0xFFFFFFFF,且 Info(典型的中级日志)在
    // 所有 >= Trace 的级别上都应通过。
    SLogCategory* Cat = MLogRegistry::Get().RegisterCategory("Test_Router_DefaultAllSinks", ELogLevel::Info);
    EXPECT_TRUE(Cat != nullptr);

    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Trace), 0xFFFFFFFFu);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Debug), 0xFFFFFFFFu);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Info), 0xFFFFFFFFu);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Warn), 0xFFFFFFFFu);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Error), 0xFFFFFFFFu);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Critical), 0xFFFFFFFFu);
}

TEST_CASE(LogRouter_SetRuleChangesMask) {
    auto& Router = MLogRouter::Get();
    Router.ClearRules();

    SLogCategory* Cat = MLogRegistry::Get().RegisterCategory("Test_Router_SetRuleChangesMask", ELogLevel::Info);
    EXPECT_TRUE(Cat != nullptr);

    // 只留 Console + File(1<<0 | 1<<1)。
    const uint32  ConsoleAndFile = (1u << static_cast<uint32>(ELogSinkId::Console)) | (1u << static_cast<uint32>(ELogSinkId::File));
    SLogRouteRule Rule;
    Rule.Category = Cat;
    Rule.SinkMask = ConsoleAndFile;
    Rule.MinLevel = ELogLevel::Trace;
    Router.SetRule(Rule);

    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Info), ConsoleAndFile);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Error), ConsoleAndFile);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Critical), ConsoleAndFile);

    // 其他分类保持默认。
    SLogCategory* Other = MLogRegistry::Get().RegisterCategory("Test_Router_SetRuleChangesMask_Other", ELogLevel::Info);
    EXPECT_TRUE(Other != nullptr);
    EXPECT_EQ(Router.ResolveSinkMask(Other->Id, ELogLevel::Info), 0xFFFFFFFFu);
}

TEST_CASE(LogRouter_MinLevelFiltersLowerLevels) {
    auto& Router = MLogRouter::Get();
    Router.ClearRules();

    SLogCategory* Cat = MLogRegistry::Get().RegisterCategory("Test_Router_MinLevelFiltersLowerLevels", ELogLevel::Trace);
    EXPECT_TRUE(Cat != nullptr);

    // 设 MinLevel = Warn:Trace/Debug/Info 应被丢弃(SinkMask=0),
    // Warn/Error/Critical 则按配置的掩码通过。
    const uint32  AnyMask = (1u << static_cast<uint32>(ELogSinkId::Console));
    SLogRouteRule Rule;
    Rule.Category = Cat;
    Rule.SinkMask = AnyMask;
    Rule.MinLevel = ELogLevel::Warn;
    Router.SetRule(Rule);

    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Trace), 0u);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Debug), 0u);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Info), 0u);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Warn), AnyMask);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Error), AnyMask);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Critical), AnyMask);

    // 越界 CategoryId 必须返回默认值(全部 sink、不过滤级别),这样新注册
    // 的分类在设置规则之前会先路由到所有 sink。
    EXPECT_EQ(Router.ResolveSinkMask(65535, ELogLevel::Info), 0xFFFFFFFFu);
}
