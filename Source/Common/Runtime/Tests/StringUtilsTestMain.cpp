// StringUtilsTest main entry — runs all TEST_CASE functions defined in the linked
// test TUs. Implementation note: TestHarness.h uses `static std::atomic<int>`
// counters per TU. To make RUN_TESTS() in this TU see the same counter instances
// that the test TU increments, we #include the test source directly. This
// collapses the test TU into main.cpp so they share internal-linkage statics
// (matches the LogTest/main.cpp pattern).
// Note: do NOT add TestStringUtilsFormat.cpp to the StringUtilsTest add_executable
// source list — it would cause ODR-multiple-definition of TEST_CASE funcs.

#include "Common/Runtime/Tests/TestHarness.h"
#include "Common/Runtime/Tests/TestStringUtilsFormat.cpp"

int main()
{
    std::printf("Running StringUtilsTest (Task 3)...\n");

    std::printf("[ MFormat_Format_IntegerSubstitution ]\n");
    Test_MFormat_Format_IntegerSubstitution();

    std::printf("[ MFormat_Format_HexAndPadding ]\n");
    Test_MFormat_Format_HexAndPadding();

    std::printf("[ MFormat_Format_InvalidRaises ]\n");
    Test_MFormat_Format_InvalidRaises();

    std::printf("[ MFormat_ToString_ArithmeticAndString ]\n");
    Test_MFormat_ToString_ArithmeticAndString();

    RUN_TESTS();
    return 0;
}