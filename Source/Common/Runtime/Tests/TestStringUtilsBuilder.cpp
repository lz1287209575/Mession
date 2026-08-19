#include "Common/Runtime/StringUtils.h"
#include "Common/Runtime/Tests/TestHarness.h"

TEST_CASE(MStringBuilder_Append_AllOverloads) {
    MStringBuilder B;

    MStringBuilder::Append(B, MStringView("view"));
    MStringBuilder::Append(B, '+');
    MStringBuilder::Append(B, "cstr");
    MStringBuilder::Append(B, MString("mstr"));
    MStringBuilder::AppendByte(B, uint8(0x0A));

    EXPECT_EQ(B.Size(), size_t(14));
    EXPECT_TRUE(MStringBuilder::ToString(B) == MString("view+cstrmstr\n"));
}

TEST_CASE(MStringBuilder_AppendFormat_MatchesFormat) {
    MStringBuilder B;
    MStringBuilder::AppendFormat(B, "x={} y={:.1f}", 7, 3.14);
    EXPECT_TRUE(MStringBuilder::ToString(B) == MFormat::Format("x={} y={:.1f}", 7, 3.14));
}

TEST_CASE(MStringBuilder_AppendFormat_Streaming) {
    MStringBuilder B(8); // 预 reserve
    MStringBuilder::Append(B, "id=");
    MStringBuilder::AppendFormat(B, "{}", 42);
    MStringBuilder::AppendByte(B, uint8(0x00));
    EXPECT_EQ(B.Size(), size_t(6));
    // 缓冲区实际内容是 "id=42\x00"（6 字节），包含 AppendByte 写入的尾零
    EXPECT_TRUE(B.View() == MStringView("id=42\x00", 6));
}