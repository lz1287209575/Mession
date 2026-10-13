/**
 * @file AwaitableTestMain.cpp
 * @brief AwaitableTest 可执行入口 —— 合并测试 TU 并逐个调用测试函数。
 */
// AwaitableTest 可执行入口:直接 include 测试 TU(匿名 namespace 在同一 TU
// 内可见),然后手动调用各测试函数 —— 模式与 FreeAsyncFuncGenTest 一致。
#include "Common/Runtime/Log/Tests/TestHarness.h"

#include "AwaitableTest.cpp"
#include "InnerTypeTest.cpp"

int main() {
    std::printf("[ SAwaiter_ReadyPath_AwaitReadyTrueAndResumeValue ]\n");
    Test_SAwaiter_ReadyPath_AwaitReadyTrueAndResumeValue();

    std::printf("[ SAwaiter_ErrPath_AwaitResumeThrows ]\n");
    Test_SAwaiter_ErrPath_AwaitResumeThrows();

    std::printf("[ SAwaiter_Suspend_ThenResumesFrameOnCompletion ]\n");
    Test_SAwaiter_Suspend_ThenResumesFrameOnCompletion();

    std::printf("[ TAwaitable_FormA_MinimalSignature ]\n");
    Test_TAwaitable_FormA_MinimalSignature();

    std::printf("[ TAwaitable_FormA_ReturnConversion ]\n");
    Test_TAwaitable_FormA_ReturnConversion();

    std::printf("[ InnerType_MatchesTemplateParameter ]\n");
    Test_InnerType_MatchesTemplateParameter();

    std::printf("[ InnerType_UsedByTAwaitableDeduction ]\n");
    Test_InnerType_UsedByTAwaitableDeduction();

    RUN_TESTS();
}
