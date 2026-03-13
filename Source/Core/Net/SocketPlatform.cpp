#include "SocketPlatform.h"
#include "Common/StringUtils.h"
#include <cstring>

namespace
{
bool IsValidSocket(TSocketFd SocketFd)
{
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return SocketFd != INVALID_SOCKET;
#else
    return SocketFd >= 0;
#endif
}
}

bool MSocketPlatform::EnsureInit()
{
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    static bool bInitialized = false;
    if (bInitialized)
    {
        return true;
    }

    WSADATA WsaData;
    if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
    {
        return false;
    }

    bInitialized = true;
    return true;
#else
    return true;
#endif
}

TSocketFd MSocketPlatform::CreateTcpSocket()
{
    if (!EnsureInit())
    {
        return INVALID_SOCKET_FD;
    }

    return socket(AF_INET, SOCK_STREAM, 0);
}

void MSocketPlatform::CloseSocket(TSocketFd SocketFd)
{
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    if (IsValidSocket(SocketFd))
    {
        closesocket(SocketFd);
    }
#else
    if (IsValidSocket(SocketFd))
    {
        close(SocketFd);
    }
#endif
}

bool MSocketPlatform::SetNonBlocking(TSocketFd SocketFd, bool bNonBlocking)
{
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    u_long Mode = bNonBlocking ? 1 : 0;
    return ioctlsocket(SocketFd, FIONBIO, &Mode) == 0;
#else
    int32 Flags = fcntl(SocketFd, F_GETFL, 0);
    if (Flags < 0)
    {
        return false;
    }

    if (bNonBlocking)
    {
        Flags |= O_NONBLOCK;
    }
    else
    {
        Flags &= ~O_NONBLOCK;
    }

    return fcntl(SocketFd, F_SETFL, Flags) == 0;
#endif
}

bool MSocketPlatform::SetNoDelay(TSocketFd SocketFd, bool bNoDelay)
{
    int32 NoDelay = bNoDelay ? 1 : 0;
    return setsockopt(SocketFd, IPPROTO_TCP, TCP_NODELAY, (const char*)&NoDelay, sizeof(NoDelay)) == 0;
}

bool MSocketPlatform::SetReuseAddress(TSocketFd SocketFd, bool bReuseAddress)
{
    int32 ReuseAddress = bReuseAddress ? 1 : 0;
    return setsockopt(SocketFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&ReuseAddress, sizeof(ReuseAddress)) == 0;
}

TSocketFd MSocketPlatform::Accept(TSocketFd ListenFd, sockaddr_in& OutAddress)
{
    OutAddress = {};
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    int AddressLen = sizeof(sockaddr_in);
#else
    socklen_t AddressLen = sizeof(sockaddr_in);
#endif
    return accept(ListenFd, (sockaddr*)&OutAddress, &AddressLen);
}

bool MSocketPlatform::GetPeerAddress(TSocketFd SocketFd, TString& OutAddress, uint16& OutPort)
{
    sockaddr_in PeerAddress = {};
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    int AddressLen = sizeof(PeerAddress);
#else
    socklen_t AddressLen = sizeof(PeerAddress);
#endif

    if (getpeername(SocketFd, (sockaddr*)&PeerAddress, &AddressLen) != 0)
    {
        OutAddress = "unknown";
        OutPort = 0;
        return false;
    }

    return DescribeAddress(PeerAddress, OutAddress, OutPort);
}

bool MSocketPlatform::DescribeAddress(const sockaddr_in& Address, TString& OutAddress, uint16& OutPort)
{
    char AddressBuffer[64] = {};
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    if (InetNtopA(AF_INET, (void*)&Address.sin_addr, AddressBuffer, sizeof(AddressBuffer)) == nullptr)
    {
        OutAddress = "unknown";
        OutPort = ntohs(Address.sin_port);
        return false;
    }
#else
    if (inet_ntop(AF_INET, (void*)&Address.sin_addr, AddressBuffer, sizeof(AddressBuffer)) == nullptr)
    {
        OutAddress = "unknown";
        OutPort = ntohs(Address.sin_port);
        return false;
    }
#endif

    OutAddress = AddressBuffer;
    OutPort = ntohs(Address.sin_port);
    return true;
}

bool MSocketPlatform::ParseIPv4Address(const SSocketAddress& Address, sockaddr_in& OutSockAddr)
{
    OutSockAddr = {};
    OutSockAddr.sin_family = AF_INET;
    OutSockAddr.sin_port = htons(Address.Port);

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return InetPtonA(AF_INET, Address.Ip.c_str(), &OutSockAddr.sin_addr) == 1;
#else
    return inet_pton(AF_INET, Address.Ip.c_str(), &OutSockAddr.sin_addr) == 1;
#endif
}

int MSocketPlatform::Connect(TSocketFd SocketFd, const sockaddr_in& Address)
{
    return connect(SocketFd, (const sockaddr*)&Address, sizeof(Address));
}

int32 MSocketPlatform::Send(TSocketFd SocketFd, const void* Data, uint32 Size)
{
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return send(SocketFd, (const char*)Data, (int)Size, 0);
#else
    return static_cast<int32>(send(SocketFd, Data, Size, 0));
#endif
}

int32 MSocketPlatform::Recv(TSocketFd SocketFd, void* Buffer, uint32 Size)
{
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return recv(SocketFd, (char*)Buffer, (int)Size, 0);
#else
    return static_cast<int32>(recv(SocketFd, Buffer, Size, 0));
#endif
}

int MSocketPlatform::GetLastError()
{
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return WSAGetLastError();
#else
    return errno;
#endif
}

FString MSocketPlatform::GetLastErrorMessage()
{
    const int Error = GetLastError();
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return MString::ToString(static_cast<uint32>(Error));
#else
    return strerror(Error);
#endif
}

bool MSocketPlatform::IsWouldBlock(int Error)
{
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return Error == WSAEWOULDBLOCK;
#else
    return Error == EWOULDBLOCK || Error == EAGAIN;
#endif
}

bool MSocketPlatform::IsConnectInProgress(int Error)
{
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return Error == WSAEWOULDBLOCK || Error == WSAEINPROGRESS;
#else
    return Error == EINPROGRESS;
#endif
}
