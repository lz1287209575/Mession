// AwaitableTest 可执行入口：include 测试 TU（匿名 namespace 同 TU 可见），
// 手动调用各测试函数（模式同 FreeAsyncFuncGenTest）。
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
