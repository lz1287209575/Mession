#include "Common/Runtime/Log/LogRegistry.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"
#include <cstring>

TEST_CASE(LogRegistry_RegisterAndLookup) {
    // Register a fresh category and verify we get a stable pointer + assigned Id.
    // Use a name unlikely to collide with any of the project-level categories
    // (LogCore/LogNet/LogDb/LogRpc/LogAuth/LogScene) defined by LogCategories.cpp.
    SLogCategory* Cat = MLogRegistry::Get().RegisterCategory("Test_Registry_RegisterAndLookup", ELogLevel::Debug);
    EXPECT_TRUE(Cat != nullptr);
    EXPECT_TRUE(Cat->Name != nullptr);
    EXPECT_EQ(std::strcmp(Cat->Name, "Test_Registry_RegisterAndLookup"), 0);
    EXPECT_EQ((int)Cat->DefaultLevel, (int)ELogLevel::Debug);
    EXPECT_EQ((int)Cat->RuntimeLevel.load(), (int)ELogLevel::Debug);
    EXPECT_EQ(Cat->bSuppressed.load(), false);
    EXPECT_EQ(Cat->DropCount.load(), uint64{0});

    // Same name -> same pointer (idempotent).
    SLogCategory* Again = MLogRegistry::Get().RegisterCategory("Test_Registry_RegisterAndLookup", ELogLevel::Error);
    EXPECT_TRUE(Again == Cat);
    // Idempotent re-registration must NOT mutate the existing entry's DefaultLevel.
    EXPECT_EQ((int)Cat->DefaultLevel, (int)ELogLevel::Debug);
}

TEST_CASE(LogRegistry_GetById) {
    SLogCategory* Cat = MLogRegistry::Get().RegisterCategory("Test_Registry_GetById", ELogLevel::Info);
    EXPECT_TRUE(Cat != nullptr);

    const SLogCategory* Found = MLogRegistry::Get().GetById(Cat->Id);
    EXPECT_TRUE(Found == Cat);

    // An out-of-range Id must return nullptr rather than crash.
    const SLogCategory* Bogus = MLogRegistry::Get().GetById(65535);
    EXPECT_TRUE(Bogus == nullptr);
}

TEST_CASE(LogRegistry_FindByNameAndCount) {
    // Snapshot baseline; other tests in this TU may have registered categories.
    const size_t Before = MLogRegistry::Get().NumCategories();

    SLogCategory* Cat = MLogRegistry::Get().RegisterCategory("Test_Registry_FindByNameAndCount", ELogLevel::Warn);
    EXPECT_TRUE(Cat != nullptr);

    EXPECT_EQ(MLogRegistry::Get().NumCategories(), Before + 1);

    const SLogCategory* Found = MLogRegistry::Get().FindByName(MString("Test_Registry_FindByNameAndCount"));
    EXPECT_TRUE(Found == Cat);

    const SLogCategory* Missing = MLogRegistry::Get().FindByName(MString("Test_Registry_NotRegistered"));
    EXPECT_TRUE(Missing == nullptr);
}
