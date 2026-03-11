#pragma once

#include <cstddef>

// Cross-platform poll() abstraction: POSIX poll on Unix, WSAPoll on Windows.
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    typedef WSAPOLLFD pollfd;
    #ifndef POLLIN
        #define POLLIN   (1 << 0)
    #endif
    #ifndef POLLOUT
        #define POLLOUT  (1 << 2)
    #endif
    #ifndef POLLERR
        #define POLLERR  (1 << 3)
    #endif
    #ifndef POLLHUP
        #define POLLHUP  (1 << 4)
    #endif

    inline int poll(pollfd* Fds, size_t Nfds, int TimeoutMs)
    {
        return WSAPoll(Fds, (ULONG)Nfds, TimeoutMs);
    }
#else
    #include <poll.h>
#endif
