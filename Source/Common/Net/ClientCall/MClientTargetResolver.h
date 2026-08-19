#pragma once

#include "Common/IO/Socket/Socket.h"
#include "Common/Runtime/MLib.h"

#include <mutex>

// MClientTargetResolver — 框架层客户端目标解析（server→client 下行推送 CallClient 用）。
//
// 职责范围：
//   - 全局唯一的「已连接客户端 INetConnection」注册表。
//   - 单个「当前目标」绑定上下文（由 MClientTargetContextGuard RAII 驱动），
//     非 Broadcast 的 CallClient 调用用它确定推送目标连接。
//   - 广播解析（返回全部已注册连接），供 MFUNCTION(Async, CallClient, Broadcast) 使用。
//
// 设计要点：
//   - 纯框架层：不暴露任何业务实体类型（阵营/队伍/...）——那是更高层的关注点。
//   - 进程内单例：多进程部署各自维护自己的 resolver（每个 Service 进程
//     拥有自己的客户端连接）。
//   - 注册/注销由业务 SDK 在玩家上线/下线时驱动；绑定/解绑由「知道当前玩家
//     连接」的请求处理代码驱动。
//   - 解析线程安全：内部用 std::mutex 保护。
class MClientTargetResolver {
    public:
    static MClientTargetResolver& Get();

    // 注册一个已连接客户端。传 nullptr 安全（no-op）。
    // 重复注册同一连接是幂等的。
    void RegisterConn(TSharedPtr<INetConnection> Conn);

    // 注销一个已连接客户端。传 nullptr 安全（no-op）。
    // 若被注销的连接正是当前绑定上下文，则同时清除绑定。
    void UnregisterConn(TSharedPtr<INetConnection> Conn);

    // 设置「当前目标」绑定上下文，供 ResolveCurrentContext 使用。
    // 每个进程同时只有一个绑定生效。优先用 RAII 守卫
    // （MClientTargetContextGuard），不要直接调用本接口。
    void BindContext(TSharedPtr<INetConnection> Conn);

    // 清除「当前目标」绑定上下文。已为空时 no-op。
    void ClearContext();

    // 把当前绑定上下文解析为恰好一个连接（无绑定或绑定连接已断开时返回空）。
    TVector<TSharedPtr<INetConnection>> ResolveCurrentContext();

    // 解析为所有「已注册且仍连接」的客户端连接。
    // 供 Broadcast 的 CallClient 调用使用。
    TVector<TSharedPtr<INetConnection>> ResolveBroadcast();

    private:
    MClientTargetResolver() = default;

    mutable std::mutex                       Mutex;
    TMap<uint64, TSharedPtr<INetConnection>> Connections;
    TSharedPtr<INetConnection>               CurrentContext;
};
