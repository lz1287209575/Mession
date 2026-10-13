/**
 * @file SocketHandle.h
 * @brief MSocketHandle - socket fd 的 RAII 独占所有者。
 *
 * @detail 职责边界:只在析构/Reset 时 close(fd),不做 shutdown,因此不保证对端能
 * 收到 FIN(需要优雅关闭请先调 MSocketPlatform::ShutdownSocket)。不可拷贝、可移动;
 * 移动后源句柄变为 INVALID_SOCKET_FD,不会 double close。
 */
#pragma once

#include "Common/IO/Socket/SocketPlatform.h"

/**
 * @brief MSocketHandle - 独占持有并自动关闭一个 socket fd。
 *
 * @detail Reset(NewFd) 会先关闭当前 fd 再接管 NewFd,因此可以直接用于 accept
 * 之后"换掉旧句柄"的场景。
 */
class MSocketHandle {
    public:
    MSocketHandle() = default;

    // 接管一个已存在的 fd(所有权转移);不校验 fd 是否有效
    explicit MSocketHandle(TSocketFd InSocketFd) : SocketFd(InSocketFd) {
    }

    ~MSocketHandle() {
        Reset();
    }

    MSocketHandle(const MSocketHandle&)            = delete;
    MSocketHandle& operator=(const MSocketHandle&) = delete;

    // 移动构造:掏空源句柄,不复制 fd
    MSocketHandle(MSocketHandle&& Other) noexcept : SocketFd(Other.Release()) {
    }

    // 移动赋值:先关掉自己的 fd,再接管源的 fd(自赋值安全)
    MSocketHandle& operator=(MSocketHandle&& Other) noexcept {
        if (this != &Other) {
            Reset(Other.Release());
        }
        return *this;
    }

    /**
     * @brief IsValid - fd 是否有效。
     * @return true 当且仅当 fd != INVALID_SOCKET_FD
     */
    bool IsValid() const {
        return SocketFd != INVALID_SOCKET_FD;
    }

    /**
     * @brief Get - 取原始 fd,不转移所有权。
     */
    TSocketFd Get() const {
        return SocketFd;
    }

    /**
     * @brief Release - 交出所有权。
     * @return 原来的 fd;本句柄被置为 INVALID_SOCKET_FD(之后析构不会关闭它)
     *
     * @detail 用于把 fd 交给一个裸 fd 接口(如 MSocket::Accept 的返回值语义)。
     */
    TSocketFd Release() {
        const TSocketFd ReleasedSocketFd = SocketFd;
        SocketFd                         = INVALID_SOCKET_FD;
        return ReleasedSocketFd;
    }

    /**
     * @brief Reset - 关闭当前 fd 并可选地接管新 fd。
     * @param NewSocketFd 新 fd;默认 INVALID_SOCKET_FD 即"只关闭不接管"
     *
     * @detail 传入等于当前值的 fd 会先关闭再存回去(变成已关闭的悬垂 fd),
     * 调用方需自行避免这种用法。
     */
    void Reset(TSocketFd NewSocketFd = INVALID_SOCKET_FD) {
        if (SocketFd != INVALID_SOCKET_FD) {
            MSocketPlatform::CloseSocket(SocketFd);
        }
        SocketFd = NewSocketFd;
    }

    private:
    TSocketFd SocketFd = INVALID_SOCKET_FD;
};
