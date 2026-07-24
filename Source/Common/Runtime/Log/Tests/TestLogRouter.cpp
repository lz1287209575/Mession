#include "Common/Runtime/Log/Tests/TestHarness.h"
#include "Common/Runtime/Log/LogRouter.h"
#include "Common/Runtime/Log/LogFilter.h"
#include "Common/Runtime/Log/LogRegistry.h"

// Router tests need a clean rules table for deterministic assertions; each test
// resets at entry so it is independent of other tests' SetRule/ClearRules calls.

TEST_CASE(LogRouter_DefaultAllSinks)
{
    auto& Router = MLogRouter::Get();
    Router.ClearRules();

    // Without any rules set, every category should resolve to the default
    // "all sinks enabled, no level filtering" behavior: SinkMask == 0xFFFFFFFF,
    // and Info (a typical mid-level entry) must pass at every level >= Trace.
    SLogCategory* Cat = MLogRegistry::Get().RegisterCategory(
        "Test_Router_DefaultAllSinks", ELogLevel::Info);
    EXPECT_TRUE(Cat != nullptr);

    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Trace), 0xFFFFFFFFu);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Debug), 0xFFFFFFFFu);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Info),  0xFFFFFFFFu);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Warn),  0xFFFFFFFFu);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Error), 0xFFFFFFFFu);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Critical), 0xFFFFFFFFu);
}

TEST_CASE(LogRouter_SetRuleChangesMask)
{
    auto& Router = MLogRouter::Get();
    Router.ClearRules();

    SLogCategory* Cat = MLogRegistry::Get().RegisterCategory(
        "Test_Router_SetRuleChangesMask", ELogLevel::Info);
    EXPECT_TRUE(Cat != nullptr);

    // Restrict to Console + File only (1<<0 | 1<<1).
    const uint32 ConsoleAndFile = (1u << static_cast<uint32>(ELogSinkId::Console)) | (1u << static_cast<uint32>(ELogSinkId::File));
    SLogRouteRule Rule;
    Rule.Category  = Cat;
    Rule.SinkMask  = ConsoleAndFile;
    Rule.MinLevel  = ELogLevel::Trace;
    Router.SetRule(Rule);

    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Info), ConsoleAndFile);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Error), ConsoleAndFile);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Critical), ConsoleAndFile);

    // Other categories remain at defaults.
    SLogCategory* Other = MLogRegistry::Get().RegisterCategory(
        "Test_Router_SetRuleChangesMask_Other", ELogLevel::Info);
    EXPECT_TRUE(Other != nullptr);
    EXPECT_EQ(Router.ResolveSinkMask(Other->Id, ELogLevel::Info), 0xFFFFFFFFu);
}

TEST_CASE(LogRouter_MinLevelFiltersLowerLevels)
{
    auto& Router = MLogRouter::Get();
    Router.ClearRules();

    SLogCategory* Cat = MLogRegistry::Get().RegisterCategory(
        "Test_Router_MinLevelFiltersLowerLevels", ELogLevel::Trace);
    EXPECT_TRUE(Cat != nullptr);

    // Set MinLevel = Warn so Trace/Debug/Info should be dropped (SinkMask=0)
    // and Warn/Error/Critical should pass with the configured mask.
    const uint32 AnyMask = (1u << static_cast<uint32>(ELogSinkId::Console));
    SLogRouteRule Rule;
    Rule.Category  = Cat;
    Rule.SinkMask  = AnyMask;
    Rule.MinLevel  = ELogLevel::Warn;
    Router.SetRule(Rule);

    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Trace), 0u);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Debug), 0u);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Info),  0u);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Warn),  AnyMask);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Error), AnyMask);
    EXPECT_EQ(Router.ResolveSinkMask(Cat->Id, ELogLevel::Critical), AnyMask);

    // Out-of-range CategoryId must return the default (all-sinks, no level filter)
    // so newly registered categories route everywhere until a rule is set.
    EXPECT_EQ(Router.ResolveSinkMask(65535, ELogLevel::Info), 0xFFFFFFFFu);
}
