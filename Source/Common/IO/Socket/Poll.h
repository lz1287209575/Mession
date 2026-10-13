/**
 * @file Poll.h
 * @brief 跨平台 poll() 抽象 + MSocketPoller(服务器主循环的就绪事件扫描器)。
 *
 * @detail 平台差异在这里一次性抹平:
 *   - POSIX:直接使用系统 <poll.h>;
 *   - Windows:没有 poll,这里 typedef WSAPOLLFD pollfd,再用 inline poll() 包一层
 *     WSAPoll,并补上 POLLIN/POLLOUT/POLLERR/POLLHUP 常量(Windows 侧只保证这几个
 *     位是可用的语义,其余 POSIX 事件位不在此抽象内)。
 * 本文件不收发数据,只回答"哪些连接现在可读/出错"。
 */
#pragma once

#include "Common/IO/Socket/Socket.h"
#include <cstddef>

// 跨平台 poll() 抽象:Unix 用 POSIX poll,Windows 用 WSAPoll
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

typedef WSAPOLLFD pollfd;
#ifndef POLLIN
#define POLLIN (1 << 0)
#endif
#ifndef POLLOUT
#define POLLOUT (1 << 2)
#endif
#ifndef POLLERR
#define POLLERR (1 << 3)
#endif
#ifndef POLLHUP
#define POLLHUP (1 << 4)
#endif

inline int poll(pollfd* Fds, size_t Nfds, int TimeoutMs) {
    return WSAPoll(Fds, (ULONG)Nfds, TimeoutMs);
}
#else
#include <poll.h>
#endif

/**
 * @brief SSocketPollItem - 单个待轮询连接。
 *
 * @detail ConnectionId 与底层 fd 并列保存:调用方据此直接定位业务对象,
 * 不必维护 fd → 连接的反查表(项目里也没有这样的表)。
 * 只填 fd 与 events,revents 由 poll() 回填。
 */
struct SSocketPollItem {
    uint64 ConnectionId = 0;
    pollfd PollFd{};
};

/**
 * @brief SSocketPollResult - 一次 poll 后"有事发生"的连接。
 *
 * @detail Revents 的位含义与 pollfd::revents 相同;判定可读/出错请走
 * MSocketPoller::IsReadable / HasError,不要自己写位运算。
 */
struct SSocketPollResult {
    uint64 ConnectionId = 0;
    short  Revents      = 0;
};

/**
 * @brief MSocketPoller - 把一批连接收敛成一次 poll() 调用。
 *
 * @detail 典型每帧用法:
 *   1. BuildReadableItems(Connections, GetConnection) 收集本轮要轮询的连接
 *      (顺带对每个连接先冲一次发送缓冲);
 *   2. PollReadable(Items, Results, TimeoutMs) 等待就绪;
 *   3. 对每个 Result 用 IsReadable/HasError 决定收包或断开。
 * 纯静态类、无内部状态;是否线程安全完全取决于调用方传入的连接表本身。
 */
class MSocketPoller {
    public:
    /**
     * @brief BuildReadableItems - 收集本轮需要 poll 的连接。
     * @param Connections   连接表,需支持 range-for 展开为 pair<ConnectionId, Entry>
     * @param GetConnection Entry → INetConnection* 的取址回调
     * @return 待轮询项列表;空指针或未连接的条目被跳过
     *
     * @detail 每项只订阅 POLLIN(写就绪不轮询:发送走 FlushSendBuffer 的非阻塞直写,
     * 写不动就留在 SendBuffer 里下轮再冲),因此在入表前先对每个连接冲一次发送缓冲。
     * 输出顺序与 Connections 的迭代顺序一致。
     */
    template <typename TConnectionMap, typename TGetConnection> static TVector<SSocketPollItem> BuildReadableItems(TConnectionMap& Connections, TGetConnection GetConnection) {
        TVector<SSocketPollItem> PollItems;
        PollItems.reserve(Connections.size());

        for (auto& [ConnectionId, Entry] : Connections) {
            INetConnection* Connection = GetConnection(Entry);
            if (!Connection || !Connection->IsConnected()) {
                continue;
            }

            Connection->FlushSendBuffer();

            SSocketPollItem Item;
            Item.ConnectionId   = ConnectionId;
            Item.PollFd.fd      = Connection->GetSocketFd();
            Item.PollFd.events  = POLLIN;
            Item.PollFd.revents = 0;
            PollItems.push_back(Item);
        }

        return PollItems;
    }

    /**
     * @brief PollReadable - 阻塞至多 TimeoutMs 毫秒并回填就绪结果。
     * @param PollItems   BuildReadableItems 的产物
     * @param OutResults  输出:revents 非 0 的项(连接 ID + 事件位);进来先被清空
     * @param TimeoutMs   poll 超时(毫秒):0 = 立即返回,-1 = 一直等
     * @return poll() 的原始返回值:> 0 就绪 fd 数、0 超时、< 0 出错;输入为空直接返回 0
     *
     * @detail OutResults 只包含"有事发生"的连接,因此它的下标与 PollItems **不对应**,
     * 必须用 ConnectionId 回查。
     */
    static int32 PollReadable(const TVector<SSocketPollItem>& PollItems, TVector<SSocketPollResult>& OutResults, int TimeoutMs) {
        OutResults.clear();
        if (PollItems.empty()) {
            return 0;
        }

        TVector<pollfd> PollFds;
        PollFds.reserve(PollItems.size());
        for (const SSocketPollItem& Item : PollItems) {
            PollFds.push_back(Item.PollFd);
        }

        const int32 Ret = poll(PollFds.data(), PollFds.size(), TimeoutMs);
        if (Ret <= 0) {
            return Ret;
        }

        OutResults.reserve(PollItems.size());
        for (size_t Index = 0; Index < PollItems.size(); ++Index) {
            if (PollFds[Index].revents == 0) {
                continue;
            }

            SSocketPollResult Result;
            Result.ConnectionId = PollItems[Index].ConnectionId;
            Result.Revents      = PollFds[Index].revents;
            OutResults.push_back(Result);
        }

        return Ret;
    }

    /**
     * @brief IsReadable - 是否有数据可读。
     * @return true 当且仅当 Revents 含 POLLIN
     */
    static bool IsReadable(const SSocketPollResult& PollResult) {
        return (PollResult.Revents & POLLIN) != 0;
    }

    /**
     * @brief HasError - 是否出错或对端挂断。
     * @return true 当且仅当 Revents 含 POLLERR 或 POLLHUP
     *
     * @detail POLLHUP 时仍可能有残余数据可读:调用方通常先收干缓冲再断开。
     */
    static bool HasError(const SSocketPollResult& PollResult) {
        return (PollResult.Revents & (POLLERR | POLLHUP)) != 0;
    }
};
