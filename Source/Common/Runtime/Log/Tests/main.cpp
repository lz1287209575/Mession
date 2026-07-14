// LogTest main entry — runs all TEST_CASE functions defined in the linked test TUs.
// Task 1 only links TestMpscRingBuffer.cpp; subsequent tasks will append more TUs.
//
// Implementation note: TestHarness.h uses `static` counters (internal linkage per TU).
// To make RUN_TESTS() in this TU see the same counter instances that the test TU
// increments, we #include the test source directly. This collapses the test TU into
// main.cpp so they share internal-linkage statics. (Alternative: switch to extern +
// single TestGlobals.cpp definition; kept off for now to match the brief verbatim.)

#include "Common/Runtime/Log/Tests/TestHarness.h"
#include "Common/Runtime/Log/Tests/TestMpscRingBuffer.cpp"

int main()
{
    std::printf("Running LogTest (Task 1)...\n");

    std::printf("[ MpscRingBuffer_Basic ]\n");
    Test_MpscRingBuffer_Basic();

    std::printf("[ MpscRingBuffer_MultiProducer ]\n");
    Test_MpscRingBuffer_MultiProducer();

    std::printf("[ MpscRingBuffer_FullReturnsFalse ]\n");
    Test_MpscRingBuffer_FullReturnsFalse();

    RUN_TESTS();
}