/**
 * @file StringUtilsTestMain.cpp
 * @brief StringUtilsTest 入口 —— 合并两个测试 TU 并逐个调用测试函数。
 *
 * 实现方式与 LogTest / AwaitableTest 一致:TestHarness.h 的计数器是每个 TU 一份
 * 的内部链接 static,因此这里直接 #include 测试源文件,让它们与本 TU 共用同一份
 * 计数。
 *
 * NOTE: 不要把 TestStringUtilsFormat.cpp / TestStringUtilsBuilder.cpp 加进
 * StringUtilsTest 的 add_executable 源列表 —— 那些 TU 里的 TEST_CASE 会被重复
 * 定义(ODR)。
 */

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