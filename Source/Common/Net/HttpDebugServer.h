#pragma once

#include "Common/Runtime/MLib.h"
#include <condition_variable>
#include <mutex>

// 极简 HTTP 调试服务器：监听本地端口，按请求返回一段文本/JSON。
// 仅用于开发/调试，不用于正式业务流量。
class MHttpDebugServer
{
public:
    using TStatusHandler = TFunction<MString()>;

    MHttpDebugServer(uint16 InPort, TStatusHandler InHandler);
    ~MHttpDebugServer();

    bool Start();
    void Stop();

    uint16 GetPort() const { return Port; }

private:
    void SignalStartResult(bool bSucceeded);
    void RunLoop();
    void HandleClient(int ClientFd);

private:
    uint16 Port = 0;
    TStatusHandler StatusHandler;
    std::thread WorkerThread;
    std::atomic<bool> bRunning{false};
    std::atomic<int> ListenFd{-1};
    std::mutex StartMutex;
    std::condition_variable StartCv;
    bool bStartPending = false;
    bool bStartSucceeded = false;
};
