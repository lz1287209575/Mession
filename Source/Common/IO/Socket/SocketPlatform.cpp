/**
 * @file SocketPlatform.cpp
 * @brief SocketPlatform.h 的平台原语实现(Windows / Unix 双分支)。
 *
 * @detail 本文件的 #if 分支只负责类型与系统调用名的差异,不改变调用语义:
 * 两个分支都返回同一种含义的结果(0/true 成功,非 0/false 失败)。
 * 所有函数都不抛异常,错误一律通过返回值与 GetLastError() 暴露。
 */
#include "Common/IO/Socket/SocketPlatform.h"
#include <cstring>

namespace {
    // fd 有效性判定:Windows 的 SOCKET 是无符号句柄,不能用 ">= 0" 判有效(恒真),
    // 必须与 INVALID_SOCKET 比较;Unix 则是 ">= 0"。
    bool IsValidSocket(TSocketFd SocketFd) {
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
        return SocketFd != INVALID_SOCKET;
#else
        return SocketFd >= 0;
#endif
    }
} // namespace

bool MSocketPlatform::EnsureInit() {
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    // 只启动一次:bInitialized 非原子,依赖"启动期单线程调用"的约定
    static bool bInitialized = false;
    if (bInitialized) {
        return true;
    }

    WSADATA WsaData;
    if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0) {
        return false;
    }

    bInitialized = true;
    return true;
#else
    return true;
#endif
}

TSocketFd MSocketPlatform::CreateTcpSocket() {
    if (!EnsureInit()) {
        return INVALID_SOCKET_FD;
    }

    // 创建出来是阻塞态;非阻塞由 SetNonBlocking 单独设置
    return socket(AF_INET, SOCK_STREAM, 0);
}

void MSocketPlatform::ShutdownSocket(TSocketFd SocketFd) {
    // shutdown 只断收发方向、不释放 fd:目的是让对端尽快收到 FIN,
    // 释放仍要由 CloseSocket 完成。无效 fd 静默忽略。
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    if (IsValidSocket(SocketFd)) {
        shutdown(SocketFd, SD_BOTH);
    }
#else
    if (IsValidSocket(SocketFd)) {
        shutdown(SocketFd, SHUT_RDWR);
    }
#endif
}

void MSocketPlatform::CloseSocket(TSocketFd SocketFd) {
    // 默认方言(SO_LINGER 未改)下 close 可能等缓冲发完,真要立刻断开应先 ShutdownSocket
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    if (IsValidSocket(SocketFd)) {
        closesocket(SocketFd);
    }
#else
    if (IsValidSocket(SocketFd)) {
        close(SocketFd);
    }
#endif
}

bool MSocketPlatform::SetNonBlocking(TSocketFd SocketFd, bool bNonBlocking) {
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    u_long Mode = bNonBlocking ? 1 : 0;
    return ioctlsocket(SocketFd, FIONBIO, &Mode) == 0;
#else
    // 读改写 flags:不能覆盖已有的 O_APPEND 等标志位
    int32 Flags = fcntl(SocketFd, F_GETFL, 0);
    if (Flags < 0) {
        return false;
    }

    if (bNonBlocking) {
        Flags |= O_NONBLOCK;
    } else {
        Flags &= ~O_NONBLOCK;
    }

    return fcntl(SocketFd, F_SETFL, Flags) == 0;
#endif
}

bool MSocketPlatform::SetNoDelay(TSocketFd SocketFd, bool bNoDelay) {
    // 关 Nagle:小包立即发出,游戏的消息式通信不吃延迟;代价是包多时带宽略增
    int32 NoDelay = bNoDelay ? 1 : 0;
    return setsockopt(SocketFd, IPPROTO_TCP, TCP_NODELAY, (const char*)&NoDelay, sizeof(NoDelay)) == 0;
}

bool MSocketPlatform::SetReuseAddress(TSocketFd SocketFd, bool bReuseAddress) {
    int32 ReuseAddress = bReuseAddress ? 1 : 0;
    return setsockopt(SocketFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&ReuseAddress, sizeof(ReuseAddress)) == 0;
}

TSocketFd MSocketPlatform::Accept(TSocketFd ListenFd, sockaddr_in& OutAddress) {
    OutAddress = {};
    // 平台差异只在长度参数类型:Winsock 要 int*,POSIX 要 socklen_t*
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    int AddressLen = sizeof(sockaddr_in);
#else
    socklen_t AddressLen = sizeof(sockaddr_in);
#endif
    return accept(ListenFd, (sockaddr*)&OutAddress, &AddressLen);
}

bool MSocketPlatform::GetPeerAddress(TSocketFd SocketFd, MString& OutAddress, uint16& OutPort) {
    sockaddr_in PeerAddress = {};
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    int AddressLen = sizeof(PeerAddress);
#else
    socklen_t AddressLen = sizeof(PeerAddress);
#endif

    if (getpeername(SocketFd, (sockaddr*)&PeerAddress, &AddressLen) != 0) {
        // 失败也给出确定的输出,避免调用方拿到空串或残留值
        OutAddress = "unknown";
        OutPort    = 0;
        return false;
    }

    return DescribeAddress(PeerAddress, OutAddress, OutPort);
}

bool MSocketPlatform::DescribeAddress(const sockaddr_in& Address, MString& OutAddress, uint16& OutPort) {
    char AddressBuffer[64] = {};
    // 转换失败通常意味着地址族不是 AF_INET;此时仍把端口按网络序还原成主机序,
    // 让调用方至少能拿到端口做日志。
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    if (InetNtopA(AF_INET, (void*)&Address.sin_addr, AddressBuffer, sizeof(AddressBuffer)) == nullptr) {
        OutAddress = "unknown";
        OutPort    = ntohs(Address.sin_port);
        return false;
    }
#else
    if (inet_ntop(AF_INET, (void*)&Address.sin_addr, AddressBuffer, sizeof(AddressBuffer)) == nullptr) {
        OutAddress = "unknown";
        OutPort    = ntohs(Address.sin_port);
        return false;
    }
#endif

    OutAddress = AddressBuffer;
    OutPort    = ntohs(Address.sin_port);
    return true;
}

bool MSocketPlatform::ParseIPv4Address(const SSocketAddress& Address, sockaddr_in& OutSockAddr) {
    // 先铺好 family/port,再只让系统解析 IP 字符串;返回 1 才代表解析成功
    OutSockAddr            = {};
    OutSockAddr.sin_family = AF_INET;
    OutSockAddr.sin_port   = htons(Address.Port);

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return InetPtonA(AF_INET, Address.Ip.c_str(), &OutSockAddr.sin_addr) == 1;
#else
    return inet_pton(AF_INET, Address.Ip.c_str(), &OutSockAddr.sin_addr) == 1;
#endif
}

int MSocketPlatform::Connect(TSocketFd SocketFd, const sockaddr_in& Address) {
    // 非阻塞 fd 上返回 EINPROGRESS 属正常:调用方用 IsConnectInProgress 判断
    return connect(SocketFd, (const sockaddr*)&Address, sizeof(Address));
}

int32 MSocketPlatform::Send(TSocketFd SocketFd, const void* Data, uint32 Size) {
    // 平台差异只在长度/指针类型(Winsock 用 int 长度与 char*);返回值语义一致,可少发
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return send(SocketFd, (const char*)Data, (int)Size, 0);
#else
    return static_cast<int32>(send(SocketFd, Data, Size, 0));
#endif
}

int32 MSocketPlatform::Recv(TSocketFd SocketFd, void* Buffer, uint32 Size) {
    // 返回值:> 0 读到的字节数,0 = 对端已关闭,< 0 = 出错(非阻塞下多为 WouldBlock)
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return recv(SocketFd, (char*)Buffer, (int)Size, 0);
#else
    return static_cast<int32>(recv(SocketFd, Buffer, Size, 0));
#endif
}

int MSocketPlatform::GetLastError() {
    // Winsock 的错误码不在 errno 里,必须走 WSAGetLastError
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return WSAGetLastError();
#else
    return errno;
#endif
}

int MSocketPlatform::GetSocketError(TSocketFd SocketFd) {
    int       Error = 0;
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    int       Length = sizeof(Error);
#else
    socklen_t Length = sizeof(Error);
#endif
    if (getsockopt(SocketFd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&Error), &Length) != 0) {
        return GetLastError();
    }
    return Error;
}

MString MSocketPlatform::GetLastErrorMessage() {
    const int Error = GetLastError();
    // Windows 分支只给十进制码:本层不引入 FormatMessage 依赖,可读文本由上层/日志补
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return std::to_string(static_cast<uint32>(Error));
#else
    return strerror(Error);
#endif
}

bool MSocketPlatform::IsWouldBlock(int Error) {
    // EWOULDBLOCK 与 EAGAIN 在 Linux/macOS 上取值相同,但标准不保证,两个都判
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return Error == WSAEWOULDBLOCK;
#else
    return Error == EWOULDBLOCK || Error == EAGAIN;
#endif
}

bool MSocketPlatform::IsConnectInProgress(int Error) {
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return Error == WSAEWOULDBLOCK || Error == WSAEINPROGRESS;
#else
    return Error == EINPROGRESS;
#endif
}

bool MSocketPlatform::IsConnectionReset(int Error) {
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    return Error == WSAECONNRESET;
#else
    return Error == ECONNRESET;
#endif
}
