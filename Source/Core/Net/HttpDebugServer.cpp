#include "HttpDebugServer.h"
#include "Common/Socket/SocketPlatform.h"
#include "Common/Log/Logger.h"
#include <cstring>
#include <chrono>

MHttpDebugServer::MHttpDebugServer(uint16 InPort, TStatusHandler InHandler)
    : Port(InPort)
    , StatusHandler(std::move(InHandler))
{
}

MHttpDebugServer::~MHttpDebugServer()
{
    Stop();
}

bool MHttpDebugServer::Start()
{
    if (bRunning.load())
    {
        return ListenFd.load() >= 0;
    }

    {
        std::lock_guard<std::mutex> Lock(StartMutex);
        bStartPending = true;
        bStartSucceeded = false;
    }
    bRunning.store(true);
    WorkerThread = std::thread(&MHttpDebugServer::RunLoop, this);

    std::unique_lock<std::mutex> Lock(StartMutex);
    StartCv.wait_for(
        Lock,
        std::chrono::milliseconds(1000),
        [this]()
        {
            return !bStartPending;
        });

    if (bStartPending)
    {
        LOG_ERROR("HttpDebugServer start timed out on 127.0.0.1:%u", static_cast<unsigned>(Port));
        Lock.unlock();
        Stop();
        return false;
    }

    if (!bStartSucceeded)
    {
        Lock.unlock();
        Stop();
        return false;
    }

    LOG_INFO("HttpDebugServer listening on http://127.0.0.1:%u/", static_cast<unsigned>(Port));
    return true;
}

void MHttpDebugServer::Stop()
{
    if (!bRunning.exchange(false))
    {
        return;
    }

    int Fd = ListenFd.exchange(-1);
    if (Fd >= 0)
    {
        MSocketPlatform::CloseSocket(Fd);
    }

    if (WorkerThread.joinable())
    {
        WorkerThread.join();
    }

    std::lock_guard<std::mutex> Lock(StartMutex);
    bStartPending = false;
    bStartSucceeded = false;
}

void MHttpDebugServer::SignalStartResult(bool bSucceeded)
{
    {
        std::lock_guard<std::mutex> Lock(StartMutex);
        bStartPending = false;
        bStartSucceeded = bSucceeded;
    }
    StartCv.notify_all();
}

void MHttpDebugServer::RunLoop()
{
    if (!MSocketPlatform::EnsureInit())
    {
        LOG_ERROR("HttpDebugServer failed to initialize socket platform");
        SignalStartResult(false);
        return;
    }

    int ServerFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (ServerFd < 0)
    {
        LOG_ERROR("HttpDebugServer failed to create socket on port %u", static_cast<unsigned>(Port));
        SignalStartResult(false);
        return;
    }

    int Opt = 1;
    ::setsockopt(ServerFd, SOL_SOCKET, SO_REUSEADDR, (char*)&Opt, sizeof(Opt));

    sockaddr_in Addr;
    std::memset(&Addr, 0, sizeof(Addr));
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons(Port);
    Addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 仅本机访问

    if (::bind(ServerFd, (sockaddr*)&Addr, sizeof(Addr)) != 0)
    {
        LOG_ERROR("HttpDebugServer bind failed on 127.0.0.1:%u", static_cast<unsigned>(Port));
        MSocketPlatform::CloseSocket(ServerFd);
        SignalStartResult(false);
        return;
    }

    if (::listen(ServerFd, 8) != 0)
    {
        LOG_ERROR("HttpDebugServer listen failed on 127.0.0.1:%u", static_cast<unsigned>(Port));
        MSocketPlatform::CloseSocket(ServerFd);
        SignalStartResult(false);
        return;
    }

    ListenFd.store(ServerFd);
    SignalStartResult(true);

    while (bRunning.load())
    {
        sockaddr_in ClientAddr;
        socklen_t ClientLen = sizeof(ClientAddr);
        int ClientFd = ::accept(ServerFd, (sockaddr*)&ClientAddr, &ClientLen);
        if (ClientFd < 0)
        {
            if (!bRunning.load())
            {
                break;
            }
            LOG_WARN("HttpDebugServer accept failed on 127.0.0.1:%u", static_cast<unsigned>(Port));
            continue;
        }

        HandleClient(ClientFd);
        MSocketPlatform::CloseSocket(ClientFd);
    }

    int Fd = ListenFd.exchange(-1);
    if (Fd >= 0)
    {
        MSocketPlatform::CloseSocket(Fd);
    }
}

void MHttpDebugServer::HandleClient(int ClientFd)
{
    char Buffer[1024];
    MString Request;

    // 读取到 header 结束或连接关闭
    while (true)
    {
        int Received = ::recv(ClientFd, Buffer, sizeof(Buffer), 0);
        if (Received <= 0)
        {
            break;
        }
        Request.append(Buffer, Buffer + Received);
        if (Request.find("\r\n\r\n") != MString::npos)
        {
            break;
        }
        if (Request.size() > 4096)
        {
            break;
        }
    }

    // 当前实现只支持 GET，忽略路径与头部，始终返回状态文本
    MString Body;
    if (StatusHandler)
    {
        Body = StatusHandler();
    }
    else
    {
        Body = "OK";
    }

    MString Response;
    Response.reserve(128 + Body.size());
    Response += "HTTP/1.1 200 OK\r\n";
    Response += "Content-Type: application/json; charset=utf-8\r\n";
    Response += "Connection: close\r\n";
    Response += "Content-Length: ";
    Response += std::to_string(Body.size());
    Response += "\r\n\r\n";
    Response += Body;

    const char* Data = Response.data();
    size_t Remaining = Response.size();
    while (Remaining > 0)
    {
        int Sent = ::send(ClientFd, Data, static_cast<int>(Remaining), 0);
        if (Sent <= 0)
        {
            break;
        }
        Data += Sent;
        Remaining -= static_cast<size_t>(Sent);
    }
}
