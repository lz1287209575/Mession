#pragma once

#include "Common/Runtime/MLib.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

enum class EEditorHttpMethod : uint8
{
    Unknown = 0,
    Get,
    Post,
    Options,
};

struct FEditorHttpRequest
{
    EEditorHttpMethod Method = EEditorHttpMethod::Unknown;
    MString RawTarget;
    MString Path;
    TMap<MString, MString> QueryParams;
    TMap<MString, MString> Headers;
    MString Body;
};

struct FEditorHttpResponse
{
    int StatusCode = 200;
    MString StatusText = "OK";
    MString ContentType = "application/json; charset=utf-8";
    TMap<MString, MString> Headers;
    MString Body;
};

class MEditorHttpServer
{
public:
    using TRequestHandler = TFunction<FEditorHttpResponse(const FEditorHttpRequest&)>;

    MEditorHttpServer(uint16 InPort, TRequestHandler InHandler);
    ~MEditorHttpServer();

    bool Start();
    void Stop();

    uint16 GetPort() const { return Port; }

private:
    void SignalStartResult(bool bSucceeded);
    void RunLoop();
    void HandleClient(int ClientFd);

private:
    uint16 Port = 0;
    TRequestHandler RequestHandler;
    std::thread WorkerThread;
    std::atomic<bool> bRunning{false};
    std::atomic<int> ListenFd{-1};
    std::mutex StartMutex;
    std::condition_variable StartCv;
    bool bStartPending = false;
    bool bStartSucceeded = false;
};
