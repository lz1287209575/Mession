/**
 * @file SubReactorPool.cpp
 * @brief MSubReactorPool 实现.
 */
#include "Common/Runtime/Concurrency/SubReactorPool.h"

#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/EventLoop/TaskEventLoop.h"
#include "Common/Runtime/Log/Log.h"

#include <cstdlib>
#include <thread>

void MSubReactorPool::Init(uint32 InSubCount) {
    if (bInitialized) {
        LOG_WARN("MSubReactorPool already initialized");
        return;
    }
    if (InSubCount == 0) {
        LOG_FATAL("MSubReactorPool::Init called with 0 subs");
        std::abort();
    }

    SubCount = InSubCount;
    SubLoops.reserve(InSubCount);
    SubTaskLoops.reserve(InSubCount);
    SubAmbients.reserve(InSubCount);
    LoopThreads.reserve(InSubCount);

    // Init 只创建对象,不启动线程 —— 让调用方(MNetServerBase::Run)
    // 先在本线程完成 Sub #0 的 RegisterListener 等初始化,再调 Start()
    // 启动线程,避免跨线程访问 Loop 内部容器(Connections / Listeners)。
    for (uint32 Index = 0; Index < InSubCount; ++Index) {
        TSharedPtr<MNetEventLoop>  Loop     = MakeShared<MNetEventLoop>();
        TSharedPtr<MTaskEventLoop> TaskLoop = MakeShared<MTaskEventLoop>();
        // 历史:MAsync namespace 待迁移到 mession::async(违反 CodingStyle §1.10)
        TSharedPtr<MAsync::MAsyncContext> Ambient = MakeShared<MAsync::MLoopAsyncContext>(TaskLoop.Get());

        SubLoops.push_back(Loop);
        SubTaskLoops.push_back(TaskLoop);
        SubAmbients.push_back(Ambient);
    }

    bInitialized = true;
    LOG_INFO("MSubReactorPool initialized with %u subs (threads not started)", static_cast<unsigned>(InSubCount));
}

void MSubReactorPool::Start() {
    if (bRunning.load()) {
        LOG_WARN("MSubReactorPool already started");
        return;
    }
    if (!bInitialized) {
        LOG_FATAL("MSubReactorPool::Start before Init");
        std::abort();
    }

    // bRunning 必须在创建线程之前置 true —— 否则 std::thread 调度
    // 启动后会立即看到 bRunning==false,while 不进,thread 立即退出。
    bRunning.store(true);

    for (uint32 Index = 0; Index < SubCount; ++Index) {
        TSharedPtr<MNetEventLoop>         Loop     = SubLoops[Index];
        TSharedPtr<MTaskEventLoop>        TaskLoop = SubTaskLoops[Index];
        TSharedPtr<MAsync::MAsyncContext> Ambient  = SubAmbients[Index];

        // 每个 Sub 一个 I/O 线程。
        // P5: Sub 线程入口装 ambient —— 让 ambient.Post(...)
        // 在这个 Sub 的 TaskLoop 上跑(回原 Sub,actor 单线程契约)
        //
        // 关键:Sub 线程要**同时**驱动 TaskLoop + NetEventLoop。
        // 如果只用 Loop->Run()(只跑 NetEventLoop),TaskLoop 队列
        // 永远不会被 drain,ambient.Post(...) 续体丢失,链路挂死。
        // 所以 Sub 线程用 while + 交替 RunOnce(TaskLoop, 0) + RunOnce(NetEventLoop, 16)。
        LoopThreads.push_back(MakeShared<std::thread>([this, Loop, TaskLoop, Ambient, Index] {
            MAsync::MAsyncContext::SetCurrent(Ambient.Get());
            LOG_INFO("Sub[%u] thread started", static_cast<unsigned>(Index));
            while (bRunning.load()) {
                TaskLoop->RunOnce(0); // 先 drain pending tasks
                Loop->RunOnce(16);    // 再 poll (16ms 阻塞,让 CPU 喘息)
            }
            LOG_INFO("Sub[%u] thread exiting", static_cast<unsigned>(Index));
        }));
    }

    LOG_INFO("MSubReactorPool started %u sub threads", static_cast<unsigned>(SubCount));
}

void MSubReactorPool::Shutdown() {
    if (bRunning.load()) {
        bRunning.store(false);

        for (auto& Loop : SubLoops) {
            if (Loop != nullptr) {
                Loop->Stop();
            }
        }
        // TaskLoop 无显式 Stop,靠 bRunning flag + RunOnce 轮询退出

        for (auto& Thread : LoopThreads) {
            if (Thread != nullptr && Thread->joinable()) {
                Thread->join();
            }
        }
    }

    LoopThreads.clear();
    SubLoops.clear();
    SubTaskLoops.clear();
    SubAmbients.clear();
    SubCount     = 0;
    bInitialized = false;

    LOG_INFO("MSubReactorPool shutdown complete");
}

MSubReactorPool::~MSubReactorPool() {
    Shutdown();
}

uint32 MSubReactorPool::PickSub(const MString& InRemoteAddr) const {
    // std::hash<MString> — 仅需稳定 + 同进程一致,不需跨平台
    // 与 Registry/Reflect 的 StableHash(FNV-1a)各自独立维护,无冲突
    const uint64 Hash = std::hash<MString>{}(InRemoteAddr);
    return static_cast<uint32>(Hash % SubCount);
}

uint32 MSubReactorPool::PickSub(uint64 InActorId) const {
    return static_cast<uint32>(InActorId % SubCount);
}

void MSubReactorPool::Post(uint32 InSubId, TFunction<void()> InTask) {
    if (InSubId >= SubTaskLoops.size()) {
        LOG_ERROR("Post to invalid SubId %u (max %u)", static_cast<unsigned>(InSubId), static_cast<unsigned>(SubTaskLoops.size()));
        return;
    }
    SubTaskLoops[InSubId]->Post(std::move(InTask));
}

MAsync::MAsyncContext* MSubReactorPool::GetAmbient(uint32 InSubId) const {
    if (InSubId >= SubAmbients.size()) {
        return nullptr;
    }
    return SubAmbients[InSubId].Get();
}

MNetEventLoop* MSubReactorPool::GetLoop(uint32 InSubId) const {
    if (InSubId >= SubLoops.size()) {
        return nullptr;
    }
    return SubLoops[InSubId].Get();
}