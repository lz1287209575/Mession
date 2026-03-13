#pragma once

#include "Core/Net/NetCore.h"
#include "SocketAddress.h"

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using TSocketFd = SOCKET;
    constexpr SOCKET INVALID_SOCKET_FD = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    using TSocketFd = int;
    constexpr int INVALID_SOCKET_FD = -1;
#endif

class MSocketPlatform
{
public:
    static bool EnsureInit();

    static TSocketFd CreateTcpSocket();
    static void CloseSocket(TSocketFd SocketFd);

    static bool SetNonBlocking(TSocketFd SocketFd, bool bNonBlocking);
    static bool SetNoDelay(TSocketFd SocketFd, bool bNoDelay);
    static bool SetReuseAddress(TSocketFd SocketFd, bool bReuseAddress);

    static TSocketFd Accept(TSocketFd ListenFd, sockaddr_in& OutAddress);
    static bool GetPeerAddress(TSocketFd SocketFd, TString& OutAddress, uint16& OutPort);
    static bool DescribeAddress(const sockaddr_in& Address, TString& OutAddress, uint16& OutPort);
    static bool ParseIPv4Address(const SSocketAddress& Address, sockaddr_in& OutSockAddr);
    static int Connect(TSocketFd SocketFd, const sockaddr_in& Address);

    static int32 Send(TSocketFd SocketFd, const void* Data, uint32 Size);
    static int32 Recv(TSocketFd SocketFd, void* Buffer, uint32 Size);

    static int GetLastError();
    static FString GetLastErrorMessage();
    static bool IsWouldBlock(int Error);
    static bool IsConnectInProgress(int Error);
};
