/**
 * @file FreeAsyncFuncGenTest.cpp
 * @brief 自由函数 async 代码生成的回归测试(P4)。
 *
 * @detail 被测入口 ProcessFreeFunctions 定义在 MHeaderToolLib.cpp(而非 MHeaderTool.cpp),
 *         这样测试能链接它而不与 MHeaderTool.cpp 的 main() 冲突。
 *         三个用例覆盖:合法的 `MFUNCTION(Async)` 自由函数被接受、带 transport 标记的
 *         自由函数被拒(异常消息里带规范章节)、注释里出现的 `MFUNCTION(Async)` 不被扫描。
 *         生成结果只做子串断言,不断言整份文件字节。
 */
#include "Core/Types.h"
#include "Generation/CodeGenerator.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"
#include "Common/Runtime/MLib.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace MHT = MHeaderTool;
namespace fs  = std::filesystem;

// 定义在 MHeaderToolLib.cpp:从 MHeaderTool.cpp 抽出来,以便测试能链接它,
// 而不与 MHeaderTool.cpp 的 main() 符号冲突。
TVector<MHT::SFreeAsyncFunc> ProcessFreeFunctions(const TMap<fs::path, MString>& FileContents);

static TMap<fs::path, MString> OneHeader(const MString& HeaderName, const MString& Body) {
    TMap<fs::path, MString> Map;
    Map[fs::path("/tmp/") / HeaderName] = Body;
    return Map;
}

TEST_CASE(FreeAsyncFunc_PlainAsync_Accepted) {
    const MString Header   = "#pragma once\n"
                             "namespace myns {\n"
                             "MFUNCTION(Async)\n"
                             "SFutureResult<int> ComputeAsync(int Seed);\n"
                             "}\n";
    auto          Contents = OneHeader("ComputeAsync.h", Header);
    auto          Funcs    = ProcessFreeFunctions(Contents);
    EXPECT_TRUE(Funcs.size() == 1);
    EXPECT_TRUE(Funcs[0].Name == "ComputeAsync");
    EXPECT_TRUE(Funcs[0].ResponseType == "int");

    // 端到端走一遍:把解析结果喂给生成器,核对 Frame 结构体名与槽位字段名。
    MHT::MCodeGenerator Gen(MHT::SOptions{});
    MString             Out = Gen.EmitFreeAsyncFramesHeader(Funcs, fs::path("/tmp/ComputeAsync.h"));
    EXPECT_TRUE(Out.find("MHeaderTool_AsyncFrame_Free_ComputeAsync") != MString::npos);
    EXPECT_TRUE(Out.find("SFutureResult<int> AwaitedSlot;") != MString::npos);
    EXPECT_TRUE(Out.find("int StoredValue") != MString::npos);
}

TEST_CASE(FreeAsyncFunc_ServerCallAsync_Rejected) {
    const MString Header   = "#pragma once\n"
                             "namespace myns {\n"
                             "MFUNCTION(ServerCall, Async)\n"
                             "SFutureResult<int> BadAsync(int Seed);\n"
                             "}\n";
    auto          Contents = OneHeader("BadAsync.h", Header);
    bool          bThrew   = false;
    try {
        ProcessFreeFunctions(Contents);
    } catch (const std::runtime_error& ex) {
        bThrew            = true;
        const MString Msg = ex.what();
        EXPECT_TRUE(Msg.find("ServerCall") != MString::npos);
        EXPECT_TRUE(Msg.find("2026-07-24") != MString::npos);
        EXPECT_TRUE(Msg.find("2026-07-28") != MString::npos);
    }
    EXPECT_TRUE(bThrew);
}

TEST_CASE(FreeAsyncFuncGenTest_HeaderWithMFUNCTIONInComment_Ignored) {
    // P4 wrap 回归:下面那行文档注释只是**行文里提到** `MFUNCTION(Async)`,不是真标记。
    // ProcessFreeFunctions 必须忽略它,只认再往下那个真标记。若 MHeaderToolLib.cpp 里少了
    // "注释预跳过"这一遍,注释文本会被 `MFUNCTION(` 子串匹配扫到,凭空多出一个形如
    // `future` / `whatever` 的假条目(该 bug 已修复,此用例是它的守卫)。
    const MString Header   = "#pragma once\n"
                             "// see MFUNCTION(Async) markers below\n"
                             "namespace myns {\n"
                             "MFUNCTION(Async)\n"
                             "SFutureResult<int> RealAsync(int Seed);\n"
                             "}\n";
    auto          Contents = OneHeader("RealAsync.h", Header);
    auto          Funcs    = ProcessFreeFunctions(Contents);
    EXPECT_TRUE(Funcs.size() == 1);
    EXPECT_TRUE(Funcs[0].Name == "RealAsync");
}

int main() {
    std::printf("Running FreeAsyncFuncGenTest (P4)\n");

    std::printf("[ FreeAsyncFunc_PlainAsync_Accepted ]\n");
    Test_FreeAsyncFunc_PlainAsync_Accepted();

    std::printf("[ FreeAsyncFunc_ServerCallAsync_Rejected ]\n");
    Test_FreeAsyncFunc_ServerCallAsync_Rejected();

    std::printf("[ FreeAsyncFuncGenTest_HeaderWithMFUNCTIONInComment_Ignored ]\n");
    Test_FreeAsyncFuncGenTest_HeaderWithMFUNCTIONInComment_Ignored();

    RUN_TESTS();
}