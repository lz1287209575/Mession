/**
 * @file SocketPlatform.h
 * @brief 把 POSIX 与 Winsock 两套 socket API 收敛成同一组类型/函数。
 *
 * @detail
 *   - 类型差异在这里一次抹平:Windows 下 TSocketFd = SOCKET、INVALID_SOCKET_FD =
 *     INVALID_SOCKET;Unix 下 TSocketFd = int、INVALID_SOCKET_FD = -1。判空一律用
 *     INVALID_SOCKET_FD 比较,不要写 == -1 或 != nullptr。
 *   - 本层是 1:1 转发 + 错误码归一:不做缓冲、不做粘包、不重试、不抛异常,
 *     失败通过返回值/GetLastError() 表达。
 *   - 地址类型统一为 sockaddr_in(IPv4),不支持 IPv6。
 */
#pragma once

#include "Common/IO/Socket/SocketAddress.h"
#include "Common/Runtime/MLib.h"

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using TSocketFd                    = SOCKET;
constexpr SOCKET INVALID_SOCKET_FD = INVALID_SOCKET;
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
using TSocketFd                 = int;
constexpr int INVALID_SOCKET_FD = -1;
#endif

/**
 * @brief MSocketPlatform - 平台 socket 原语封装(全部静态方法,无状态)。
 */
class MSocketPlatform {
    public:
    /**
     * @brief EnsureInit - 平台级初始化。
     * @return true 可用;false Windows 上 WSAStartup(2.2) 失败
     *
     * @detail Windows 用函数内静态标志保证只启动一次(非线程安全,假定在启动期调用);
     * Unix 恒返回 true。每个会碰 socket 的入口都应先调用它。
     */
    static bool EnsureInit();

    /**
     * @brief CreateTcpSocket - 创建 TCP socket(AF_INET / SOCK_STREAM)。
     * @return 新 fd;失败返回 INVALID_SOCKET_FD
     *
     * @detail 创建后是阻塞态,需要非阻塞请再调 SetNonBlocking()。
     */
    static TSocketFd CreateTcpSocket();

    /**
     * @brief ShutdownSocket - 关闭收发方向(shutdown,不释放 fd)。
     * @param SocketFd 目标 fd
     *
     * @detail 会让对端收到 FIN/RST,是"优雅关闭"的第一步;fd 仍需要 CloseSocket 释放。
     * 无效 fd 会被静默忽略。
     */
    static void ShutdownSocket(TSocketFd SocketFd);

    /**
     * @brief CloseSocket - 释放 fd 资源(close / closesocket)。
     * @param SocketFd 目标 fd;无效 fd 会被静默忽略
     */
    static void CloseSocket(TSocketFd SocketFd);

    /**
     * @brief SetNonBlocking - 设置阻塞/非阻塞模式。
     * @param SocketFd     目标 fd
     * @param bNonBlocking true = 非阻塞
     * @return true 设置成功
     */
    static bool SetNonBlocking(TSocketFd SocketFd, bool bNonBlocking);

    /**
     * @brief SetNoDelay - 设置 TCP_NODELAY。
     * @param SocketFd  目标 fd
     * @param bNoDelay  true = 关闭 Nagle 算法(小包立即发出,降低 RTT)
     * @return true 设置成功
     */
    static bool SetNoDelay(TSocketFd SocketFd, bool bNoDelay);

    /**
     * @brief SetReuseAddress - 设置 SO_REUSEADDR。
     * @param SocketFd        目标 fd
     * @param bReuseAddress   true = 允许复用处于 TIME_WAIT 的本地地址
     * @return true 设置成功
     *
     * @detail 监听 socket 必须开,否则进程重启会因旧连接的 TIME_WAIT 而 bind 失败。
     */
    static bool SetReuseAddress(TSocketFd SocketFd, bool bReuseAddress);

    /**
     * @brief Accept - 接受一个新连接。
     * @param ListenFd    监听 fd
     * @param OutAddress  输出:对端 sockaddr_in(失败时被清零)
     * @return 新连接 fd;失败(含"暂时没有新连接")返回 INVALID_SOCKET_FD
     *
     * @detail 会调用系统的 accept(可能阻塞);非阻塞监听 fd 上无连接时返回无效 fd 并
     * 置上次错误为 WouldBlock,调用方按这个错误判定"正常空轮询"。
     */
    static TSocketFd Accept(TSocketFd ListenFd, sockaddr_in& OutAddress);

    /**
     * @brief GetPeerAddress - 取对端地址。
     * @param SocketFd   已连接的 fd
     * @param OutAddress 输出:点分十进制 IP
     * @param OutPort    输出:端口(主机字节序)
     * @return true 成功;false 时 OutAddress = "unknown"、OutPort = 0
     */
    static bool GetPeerAddress(TSocketFd SocketFd, MString& OutAddress, uint16& OutPort);

    /**
     * @brief DescribeAddress - sockaddr_in → 字符串 IP + 主机字节序端口。
     * @param Address    输入地址
     * @param OutAddress 输出:点分十进制 IP
     * @param OutPort    输出:端口(主机字节序)
     * @return true 转换成功;false 时 OutAddress = "unknown",但 OutPort 仍被填好
     */
    static bool DescribeAddress(const sockaddr_in& Address, MString& OutAddress, uint16& OutPort);

    /**
     * @brief ParseIPv4Address - "ip:port" 值对象 → sockaddr_in。
     * @param Address      输入地址(Port 用主机字节序,内部转网络字节序)
     * @param OutSockAddr  输出;sockaddr_in 会被先清零并预填 family/port
     * @return true 成功;false 表示 Ip 不是合法点分十进制(此时 OutSockAddr 已填了
     *         family/port,sin_addr 仍为 0)
     */
    static bool ParseIPv4Address(const SSocketAddress& Address, sockaddr_in& OutSockAddr);

    /**
     * @brief Connect - 发起 TCP 连接。
     * @param SocketFd 本地 fd
     * @param Address  目标地址
     * @return 0 成功;非 0 失败(非阻塞 fd 上 EINPROGRESS/WSAEWOULDBLOCK 表示连接
     *         正在进行,应交给 IsConnectInProgress 判断)
     */
    static int Connect(TSocketFd SocketFd, const sockaddr_in& Address);

    /**
     * @brief Send - 发送一次。
     * @param SocketFd 目标 fd
     * @param Data     数据首地址
     * @param Size     字节数
     * @return 实际发出的字节数;0 对端已关闭;< 0 出错(非阻塞下可能是 WouldBlock)
     *
     * @detail 可以少发,调用方必须按返回值推进游标,不能假定一次发完。
     */
    static int32 Send(TSocketFd SocketFd, const void* Data, uint32 Size);

    /**
     * @brief Recv - 接收一次。
     * @param SocketFd 目标 fd
     * @param Buffer   接收缓冲
     * @param Size     缓冲大小
     * @return 实际读到的字节数;0 对端已关闭;< 0 出错(非阻塞下通常是 WouldBlock)
     */
    static int32 Recv(TSocketFd SocketFd, void* Buffer, uint32 Size);

    /**
     * @brief GetLastError - 上一次 socket 操作的错误码。
     * @return Windows: WSAGetLastError();Unix: errno
     */
    static int GetLastError();

    /**
     * @brief GetLastErrorMessage - 错误码的可读形式。
     * @return Unix: strerror 文本;Windows: 仅十进制错误码字符串(本层未接 FormatMessage)
     */
    static MString GetLastErrorMessage();

    /**
     * @brief IsWouldBlock - 该错误码是否表示"暂时不可收发,稍后重试"。
     * @return Unix: EWOULDBLOCK / EAGAIN;Windows: WSAEWOULDBLOCK
     */
    static bool IsWouldBlock(int Error);

    /**
     * @brief IsConnectInProgress - 该错误码是否表示非阻塞 connect 正在进行。
     */
    static bool IsConnectInProgress(int Error);

    /**
     * @brief IsConnectionReset - 该错误码是否表示对端复位连接。
     */
    static bool IsConnectionReset(int Error);
};
