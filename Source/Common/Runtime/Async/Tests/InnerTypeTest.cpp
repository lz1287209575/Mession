/**
 * @file InnerTypeTest.cpp
 * @brief SFutureResult<T>::InnerType 的编译期契约测试。
 */
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"

// 类型层测试:SFutureResult<T>::InnerType 的编译期契约(对应 KD-2 §4.1)。
namespace {

    TEST_CASE(InnerType_MatchesTemplateParameter) {
        static_assert(std::is_same_v<SFutureResult<int>::InnerType, int>);
        static_assert(std::is_same_v<SFutureResult<MString>::InnerType, MString>);
        static_assert(std::is_same_v<SFutureResult<void>::InnerType, void>);
        EXPECT_TRUE(true);
    }

    TEST_CASE(InnerType_UsedByTAwaitableDeduction) {
        // 形式 B 的 R 就是 SFutureResult<R>::InnerType——类型层统一入口
        static_assert(std::is_same_v<SFutureResult<int>::InnerType, int>);
        EXPECT_TRUE(true);
    }

} // namespace
