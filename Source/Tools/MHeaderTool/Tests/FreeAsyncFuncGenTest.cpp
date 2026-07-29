#include "Core/Types.h"
#include "Generation/CodeGenerator.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace MHT = MHeaderTool;
namespace fs = std::filesystem;

// Defined in MHeaderToolLib.cpp (extracted from MHeaderTool.cpp so tests can
// link the function without colliding with MHeaderTool.cpp's main()).
std::vector<MHT::SFreeAsyncFunc> ProcessFreeFunctions(
    const std::map<fs::path, std::string>& FileContents);

static TMap<fs::path, std::string> OneHeader(
    const std::string& HeaderName,
    const std::string& Body)
{
    TMap<fs::path, std::string> Map;
    Map[fs::path("/tmp/") / HeaderName] = Body;
    return Map;
}

TEST_CASE(FreeAsyncFunc_PlainAsync_Accepted)
{
    const std::string Header =
        "#pragma once\n"
        "namespace myns {\n"
        "MFUNCTION(Async)\n"
        "SFutureResult<int> ComputeAsync(int Seed);\n"
        "}\n";
    auto Contents = OneHeader("ComputeAsync.h", Header);
    auto Funcs = ProcessFreeFunctions(Contents);
    EXPECT_TRUE(Funcs.size() == 1);
    EXPECT_TRUE(Funcs[0].Name == "ComputeAsync");
    EXPECT_TRUE(Funcs[0].ResponseType == "int");

    // Round-trip: feed into the generator and verify the Frame struct name.
    MHT::CodeGenerator Gen(MHT::SOptions{});
    std::string Out = Gen.EmitFreeAsyncFramesHeader(
        Funcs, fs::path("/tmp/ComputeAsync.h"));
    EXPECT_TRUE(Out.find("MHeaderTool_AsyncFrame_Free_ComputeAsync") != std::string::npos);
    EXPECT_TRUE(Out.find("SFutureResult<int> AwaitedSlot;") != std::string::npos);
    EXPECT_TRUE(Out.find("int StoredValue") != std::string::npos);
}

TEST_CASE(FreeAsyncFunc_ServerCallAsync_Rejected)
{
    const std::string Header =
        "#pragma once\n"
        "namespace myns {\n"
        "MFUNCTION(ServerCall, Async)\n"
        "SFutureResult<int> BadAsync(int Seed);\n"
        "}\n";
    auto Contents = OneHeader("BadAsync.h", Header);
    bool bThrew = false;
    try
    {
        ProcessFreeFunctions(Contents);
    }
    catch (const std::runtime_error& ex)
    {
        bThrew = true;
        const std::string Msg = ex.what();
        EXPECT_TRUE(Msg.find("ServerCall") != std::string::npos);
        EXPECT_TRUE(Msg.find("2026-07-24") != std::string::npos);
        EXPECT_TRUE(Msg.find("2026-07-28") != std::string::npos);
    }
    EXPECT_TRUE(bThrew);
}

TEST_CASE(FreeAsyncFuncGenTest_HeaderWithMFUNCTIONInComment_Ignored)
{
    // P4 wrap regression: the doc comment line below mentions
    // `MFUNCTION(Async)` purely as documentation, not as a real marker.
    // ProcessFreeFunctions must ignore it and only pick up the real marker
    // further down. Without the comment-skip pre-pass in MHeaderToolLib.cpp,
    // a fake `future`/`whatever` entry used to appear because comment bytes
    // were scanned by the literal `MFUNCTION(` substring search.
    const std::string Header =
        "#pragma once\n"
        "// see MFUNCTION(Async) markers below\n"
        "namespace myns {\n"
        "MFUNCTION(Async)\n"
        "SFutureResult<int> RealAsync(int Seed);\n"
        "}\n";
    auto Contents = OneHeader("RealAsync.h", Header);
    auto Funcs = ProcessFreeFunctions(Contents);
    EXPECT_TRUE(Funcs.size() == 1);
    EXPECT_TRUE(Funcs[0].Name == "RealAsync");
}

int main()
{
    std::printf("Running FreeAsyncFuncGenTest (P4)\n");

    std::printf("[ FreeAsyncFunc_PlainAsync_Accepted ]\n");
    Test_FreeAsyncFunc_PlainAsync_Accepted();

    std::printf("[ FreeAsyncFunc_ServerCallAsync_Rejected ]\n");
    Test_FreeAsyncFunc_ServerCallAsync_Rejected();

    std::printf("[ FreeAsyncFuncGenTest_HeaderWithMFUNCTIONInComment_Ignored ]\n");
    Test_FreeAsyncFuncGenTest_HeaderWithMFUNCTIONInComment_Ignored();

    RUN_TESTS();
}