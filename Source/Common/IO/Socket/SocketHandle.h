#pragma once

#include "Common/IO/Socket/SocketPlatform.h"

class MSocketHandle
{
public:
    MSocketHandle() = default;

    explicit MSocketHandle(TSocketFd InSocketFd)
        : SocketFd(InSocketFd)
    {
    }

    ~MSocketHandle()
    {
        Reset();
    }

    MSocketHandle(const MSocketHandle&) = delete;
    MSocketHandle& operator=(const MSocketHandle&) = delete;

    MSocketHandle(MSocketHandle&& Other) noexcept
        : SocketFd(Other.Release())
    {
    }

    MSocketHandle& operator=(MSocketHandle&& Other) noexcept
    {
        if (this != &Other)
        {
            Reset(Other.Release());
        }
        return *this;
    }

    bool IsValid() const
    {
        return SocketFd != INVALID_SOCKET_FD;
    }

    TSocketFd Get() const
    {
        return SocketFd;
    }

    TSocketFd Release()
    {
        const TSocketFd ReleasedSocketFd = SocketFd;
        SocketFd = INVALID_SOCKET_FD;
        return ReleasedSocketFd;
    }

    void Reset(TSocketFd NewSocketFd = INVALID_SOCKET_FD)
    {
        if (SocketFd != INVALID_SOCKET_FD)
        {
            MSocketPlatform::CloseSocket(SocketFd);
        }
        SocketFd = NewSocketFd;
    }

private:
    TSocketFd SocketFd = INVALID_SOCKET_FD;
};
