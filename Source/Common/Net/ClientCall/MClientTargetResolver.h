/**
 * @file MClientTargetResolver.h
 * @brief MClientTargetResolver - 框架层的客户端目标解析(server→client 下行推送用)。
 *
 * @detail 三件事:
 *   1. 一份"已连接客户端 INetConnection"注册表;
 *   2. 一个"当前目标"绑定上下文(由 MClientTargetContextGuard 驱动),非 Broadcast 的
 *      CallClient 用它确定推送目标;
 *   3. 广播解析(返回全部已注册且仍连接的连接)。
 *
 *   设计约束:
 *   - 纯框架层:不暴露任何业务实体概念(阵营/队伍等),那是更高层的关注点。
 *   - 进程内单例:多进程部署下每个 Service 进程各自维护自己的一份(各自持有自己的
 *     客户端连接)。
 *   - 注册/注销由业务层在玩家上线/下线时驱动(如 MGatewayServer::OnAccept 的
 *     连接生命周期回调);绑定/解绑由"知道当前玩家连接"的请求处理代码驱动。
 *   - 所有公开接口都由内部 std::mutex 保护,可从任意线程调用。
 *
 *   注册表以连接对象身份为键，而非尚未完成鉴权前常为 0 的 PlayerId；因此每条已接入
 *   连接都会参与广播。当前目标上下文是 thread_local，避免不同 reactor 线程互相覆盖。
 */

#pragma once

#include "Common/IO/Socket/Socket.h"
#include "Common/Runtime/MLib.h"

#include <mutex>

/**
 * @brief MClientTargetResolver - 进程内单例的客户端连接注册表 + 当前目标绑定。
 */
class MClientTargetResolver {
    public:
    /**
     * @brief Get - 取进程内单例(Meyers 单例,首次调用时构造,进程生命周期)。
     * @return 全局唯一的 resolver 引用
     */
    static MClientTargetResolver& Get();

    /**
     * @brief RegisterConn - 注册一个已连接客户端。
     * @param Conn 客户端连接;传 nullptr 为空操作
     *
     * @detail 以 Conn 的对象地址为键：同一连接重复注册幂等，不同连接互不覆盖。
     */
    void RegisterConn(TSharedPtr<INetConnection> Conn);

    /**
     * @brief UnregisterConn - 注销一个已连接客户端。
     * @param Conn 客户端连接;传 nullptr 为空操作
     *
     * @detail 若被注销的连接正是当前绑定上下文,会一并清除绑定,避免把下行推给一个
     *   已经下线的连接。
     */
    void UnregisterConn(TSharedPtr<INetConnection> Conn);

    /**
     * @brief BindContext - 设置"当前目标"绑定上下文。
     * @param Conn 目标连接;传 nullptr 等价于 ClearContext
     *
     * @detail 优先使用 RAII 守卫 MClientTargetContextGuard 而不要直接调用本接口,
     *   否则容易忘记配对清除。每个进程同一时刻只有一个绑定生效。
     */
    void BindContext(TSharedPtr<INetConnection> Conn);

    /**
     * @brief ClearContext - 清除"当前目标"绑定上下文。已为空时为空操作。
     */
    void ClearContext();

    /// @brief 原子替换当前线程的目标上下文，并返回先前绑定；供 RAII 守卫正确恢复嵌套上下文。
    TSharedPtr<INetConnection> ExchangeContext(TSharedPtr<INetConnection> Conn);

    /**
     * @brief ResolveCurrentContext - 把当前绑定上下文解析为连接列表。
     * @return 0 个元素:没有绑定,或绑定连接已断开;1 个元素:可用的绑定连接
     *
     * @detail 返回容器而非裸指针,是为了与 ResolveBroadcast 的调用形式统一
     *   (CallClient 对两者走同一个循环)。
     */
    TVector<TSharedPtr<INetConnection>> ResolveCurrentContext();

    /**
     * @brief ResolveBroadcast - 解析为所有"已注册且仍连接"的客户端连接。
     * @return 全部可用连接的快照(已拷贝 TSharedPtr,调用期间不受后续注册/注销影响)
     *
     * @detail 供 Broadcast 的 CallClient 使用。断开但尚未注销的连接会被跳过。
     */
    TVector<TSharedPtr<INetConnection>> ResolveBroadcast();

    private:
    MClientTargetResolver() = default;

    mutable std::mutex                                      Mutex;       ///< 保护 Connections
    TMap<const INetConnection*, TSharedPtr<INetConnection>> Connections; ///< 键 = 连接对象身份
    static thread_local TSharedPtr<INetConnection>          CurrentContext;
};
