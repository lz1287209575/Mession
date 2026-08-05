#pragma once
#include <cstdio>
#include <atomic>
#include <vector>
#include <algorithm>

// NOTE: GTestPassed/GTestFailed 均为 static (内部链接),每个测试 TU 各持一份。
// 这避免了多 TU 链接符号冲突,但意味着统计是按 TU 内的测试聚合的。
// 单测试可执行文件下,把测试 TU 通过 #include 合并到 main.cpp 即可共享计数。
static std::atomic<int> GTestPassed{0};
static std::atomic<int> GTestFailed{0};

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        std::printf("  FAIL: %s (line %d)\n", #expr, __LINE__); \
        GTestFailed.fetch_add(1); \
    } else { \
        GTestPassed.fetch_add(1); \
    } \
} while(0)

#define EXPECT_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a != _b) { \
        std::printf("  FAIL: %s == %s (got %lld, expected %lld) (line %d)\n", \
            #a, #b, (long long)_a, (long long)_b, __LINE__); \
        GTestFailed.fetch_add(1); \
    } else { \
        GTestPassed.fetch_add(1); \
    } \
} while(0)

#define EXPECT_NE(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a == _b) { \
        std::printf("  FAIL: %s != %s (got %lld) (line %d)\n", \
            #a, #b, (long long)_a, __LINE__); \
        GTestFailed.fetch_add(1); \
    } else { \
        GTestPassed.fetch_add(1); \
    } \
} while(0)

#define TEST_CASE(Name) void Test_##Name()

#define RUN_TESTS() do { \
    std::printf("\n=== Results: %d passed, %d failed ===\n", \
        GTestPassed.load(), GTestFailed.load()); \
    return GTestFailed.load() == 0 ? 0 : 1; \
} while(0)
