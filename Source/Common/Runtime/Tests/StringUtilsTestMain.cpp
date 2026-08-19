// StringUtilsTest main entry — runs all TEST_CASE functions defined in the linked
// test TUs. Implementation note: TestHarness.h uses `static std::atomic<int>`
// counters per TU. To make RUN_TESTS() in this TU see the same counter instances
// that the test TU increments, we #include the test source directly. This
// collapses the test TU into main.cpp so they share internal-linkage statics
// (matches the LogTest/main.cpp pattern).
// Note: do NOT add TestStringUtilsFormat.cpp / TestStringUtilsBuilder.cpp to the
// StringUtilsTest add_executable source list — it would cause ODR-multiple-
// definition of TEST_CASE funcs.

#include "Common/Runtime/Tests/TestStringUtilsBuilder.cpp"
#include "Common/Runtime/Tests/TestStringUtilsFormat.cpp"
#include "Common/Runtime/Tests/TestHarness.h"

int main() {
    std::printf("Running StringUtilsTest...\n");

    std::printf("[ MFormat_Format_IntegerSubstitution ]\n");
    Test_MFormat_Format_IntegerSubstitution();

    std::printf("[ MFormat_Format_HexAndPadding ]\n");
    Test_MFormat_Format_HexAndPadding();

    std::printf("[ MFormat_Format_InvalidRaises ]\n");
    Test_MFormat_Format_InvalidRaises();

    std::printf("[ MFormat_ToString_ArithmeticAndString ]\n");
    Test_MFormat_ToString_ArithmeticAndString();

    std::printf("[ MStringUtil_ToString_DelegatesToMFormat ]\n");
    Test_MStringUtil_ToString_DelegatesToMFormat();

    std::printf("[ MStringBuilder_StateQueries ]\n");
    Test_MStringBuilder_StateQueries();

    std::printf("[ MStringBuilder_Append_AllOverloads ]\n");
    Test_MStringBuilder_Append_AllOverloads();

    std::printf("[ MStringBuilder_AppendFormat_MatchesFormat ]\n");
    Test_MStringBuilder_AppendFormat_MatchesFormat();

    std::printf("[ MStringBuilder_AppendFormat_Streaming ]\n");
    Test_MStringBuilder_AppendFormat_Streaming();

    RUN_TESTS();
}