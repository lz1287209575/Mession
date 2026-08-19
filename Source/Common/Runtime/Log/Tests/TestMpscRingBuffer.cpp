#include "Common/Runtime/Log/MpscRingBuffer.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"
#include <atomic>
#include <thread>
#include <vector>

struct TestRecord {
    int Value;
    int ThreadId;
};

TEST_CASE(MpscRingBuffer_Basic) {
    TMpscRingBuffer<TestRecord> Q(1024);
    TestRecord                  R{42, 0};

    EXPECT_TRUE(Q.TryEnqueue(R) == true);
    TestRecord Out[1] = {};
    EXPECT_EQ(Q.DequeueBatch(Out, 1), 1u);
    EXPECT_EQ(Out[0].Value, 42);
}

TEST_CASE(MpscRingBuffer_MultiProducer) {
    TMpscRingBuffer<TestRecord> Q(65536);
    std::atomic<int>            Produced{0};
    std::atomic<int>            Consumed{0};
    constexpr int               Threads   = 8;
    constexpr int               PerThread = 10000;

    std::vector<std::thread> Prods;
    for (int t = 0; t < Threads; ++t) {
        Prods.emplace_back([&Q, &Produced, t] {
            for (int i = 0; i < PerThread; ++i) {
                TestRecord R{i, t};
                if (Q.TryEnqueue(R))
                    Produced.fetch_add(1);
            }
        });
    }

    std::thread Cons([&Q, &Consumed] {
        TestRecord Out[256];
        while (Consumed.load() < Threads * PerThread) {
            size_t N = Q.DequeueBatch(Out, 256);
            Consumed.fetch_add((int)N);
        }
    });

    for (auto& P : Prods)
        P.join();
    Cons.join();

    EXPECT_EQ(Produced.load(), Threads * PerThread);
    EXPECT_EQ(Consumed.load(), Produced.load());
}

TEST_CASE(MpscRingBuffer_FullReturnsFalse) {
    TMpscRingBuffer<TestRecord> Q(4);
    TestRecord                  R{1, 0};
    for (int i = 0; i < 4; ++i)
        Q.TryEnqueue(R);
    EXPECT_TRUE(Q.TryEnqueue(R) == false); // 满
}