#include "HttpDebugServer.h"
#include "SocketPlatform.h"
#include <cstring>

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
        return true;
    }

    bRunning.store(true);
    WorkerThread = std::thread(&MHttpDebugServer::RunLoop, this);
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
}

void MHttpDebugServer::RunLoop()
{
    if (!MSocketPlatform::EnsureInit())
    {
        return;
    }

    int ServerFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (ServerFd < 0)
    {
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
        MSocketPlatform::CloseSocket(ServerFd);
        return;
    }

    if (::listen(ServerFd, 8) != 0)
    {
        MSocketPlatform::CloseSocket(ServerFd);
        return;
    }

    ListenFd.store(ServerFd);

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
    TString Request;

    // 读取到 header 结束或连接关闭
    while (true)
    {
        int Received = ::recv(ClientFd, Buffer, sizeof(Buffer), 0);
        if (Received <= 0)
        {
            break;
        }
        Request.append(Buffer, Buffer + Received);
        if (Request.find("\r\n\r\n") != TString::npos)
        {
            break;
        }
        if (Request.size() > 4096)
        {
            break;
        }
    }

    // 当前实现只支持 GET，忽略路径与头部，始终返回状态文本
    FString Body;
    if (StatusHandler)
    {
        Body = StatusHandler();
    }
    else
    {
        Body = "OK";
    }

    TString Response;
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

