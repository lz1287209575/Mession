#include "Common/Runtime/StringUtils.h"
#include "Common/Runtime/Tests/TestHarness.h"

TEST_CASE(MFormat_Format_IntegerSubstitution) {
    EXPECT_TRUE(MFormat::Format("id={}", 42) == MString("id=42"));
    EXPECT_TRUE(MFormat::Format("{} {} {}", 1, 2, 3) == MString("1 2 3"));
}

TEST_CASE(MFormat_Format_HexAndPadding) {
    EXPECT_TRUE(MFormat::Format("{:x}", 0xab) == MString("ab"));
    EXPECT_TRUE(MFormat::Format("{:08x}", 0xab) == MString("000000ab"));
    EXPECT_TRUE(MFormat::Format("{:.3f}", 3.14159) == MString("3.142"));
}

TEST_CASE(MFormat_Format_InvalidRaises) {
    bool bThrew = false;
    try {
        MFormat::Format("{5}", 1);
    } // 越界位置引用 → fmt 抛异常
    catch (const fmt::format_error&) {
        bThrew = true;
    }
    EXPECT_TRUE(bThrew);
}

TEST_CASE(MFormat_ToString_ArithmeticAndString) {
    EXPECT_TRUE(MFormat::ToString(int32(42)) == MString("42"));
    EXPECT_TRUE(MFormat::ToString(int64(-7)) == MString("-7"));
    EXPECT_TRUE(MFormat::ToString(uint32(0xff)) == MString("255"));
    EXPECT_TRUE(MFormat::ToString(double64(1.5)) == MString("1.5"));
    EXPECT_TRUE(MFormat::ToString(MString("plain")) == MString("plain"));
}

TEST_CASE(MStringUtil_ToString_DelegatesToMFormat) {
    EXPECT_TRUE(MStringUtil::ToString(int32(42)) == MString("42"));
    EXPECT_TRUE(MStringUtil::ToString(double64(2.5)) == MString("2.5"));
}

TEST_CASE(MStringBuilder_StateQueries) {
    MStringBuilder B;
    EXPECT_TRUE(B.Empty());
    EXPECT_EQ(B.Size(), size_t(0));
    EXPECT_EQ(B.Capacity(), size_t(0));

    MStringBuilder B2(64);
    EXPECT_EQ(B2.Size(), size_t(0));
    EXPECT_TRUE(B2.Capacity() >= 64);

    MStringBuilder::Reserve(B, 128);
    EXPECT_TRUE(B.Capacity() >= 128);

    MStringBuilder::Clear(B);
    EXPECT_EQ(B.Size(), size_t(0));
}