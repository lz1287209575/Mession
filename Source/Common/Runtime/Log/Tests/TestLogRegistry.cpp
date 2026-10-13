/**
 * @file TestLogRegistry.cpp
 * @brief MLogRegistry 分类注册行为测试(幂等、越界、按名查找)。
 */
#include "Common/Runtime/Log/LogRegistry.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"
#include <cstring>

TEST_CASE(LogRegistry_RegisterAndLookup) {
    // 注册一个新分类,验证拿到稳定的指针与分配好的 Id。名字要尽量不与
    // 项目级分类(LogCategories.cpp 里的 LogCore/LogNet/LogDb/LogRpc/
    // LogAuth/LogScene)重名。
    SLogCategory* Cat = MLogRegistry::Get().RegisterCategory("Test_Registry_RegisterAndLookup", ELogLevel::Debug);
    EXPECT_TRUE(Cat != nullptr);
    EXPECT_TRUE(Cat->Name != nullptr);
    EXPECT_EQ(std::strcmp(Cat->Name, "Test_Registry_RegisterAndLookup"), 0);
    EXPECT_EQ((int)Cat->DefaultLevel, (int)ELogLevel::Debug);
    EXPECT_EQ((int)Cat->RuntimeLevel.load(), (int)ELogLevel::Debug);
    EXPECT_EQ(Cat->bSuppressed.load(), false);
    EXPECT_EQ(Cat->DropCount.load(), uint64{0});

    // 同名 → 同一指针(幂等)。
    SLogCategory* Again = MLogRegistry::Get().RegisterCategory("Test_Registry_RegisterAndLookup", ELogLevel::Error);
    EXPECT_TRUE(Again == Cat);
    // 幂等重注册不得改写已有条目的 DefaultLevel。
    EXPECT_EQ((int)Cat->DefaultLevel, (int)ELogLevel::Debug);
}

TEST_CASE(LogRegistry_GetById) {
    SLogCategory* Cat = MLogRegistry::Get().RegisterCategory("Test_Registry_GetById", ELogLevel::Info);
    EXPECT_TRUE(Cat != nullptr);

    const SLogCategory* Found = MLogRegistry::Get().GetById(Cat->Id);
    EXPECT_TRUE(Found == Cat);

    // 越界 Id 必须返回 nullptr,而不是崩。
    const SLogCategory* Bogus = MLogRegistry::Get().GetById(65535);
    EXPECT_TRUE(Bogus == nullptr);
}

TEST_CASE(LogRegistry_FindByNameAndCount) {
    // 先取基线:本 TU 内其他用例可能已经注册过分类。
    const size_t Before = MLogRegistry::Get().NumCategories();

    SLogCategory* Cat = MLogRegistry::Get().RegisterCategory("Test_Registry_FindByNameAndCount", ELogLevel::Warn);
    EXPECT_TRUE(Cat != nullptr);

    EXPECT_EQ(MLogRegistry::Get().NumCategories(), Before + 1);

    const SLogCategory* Found = MLogRegistry::Get().FindByName(MString("Test_Registry_FindByNameAndCount"));
    EXPECT_TRUE(Found == Cat);

    const SLogCategory* Missing = MLogRegistry::Get().FindByName(MString("Test_Registry_NotRegistered"));
    EXPECT_TRUE(Missing == nullptr);
}
