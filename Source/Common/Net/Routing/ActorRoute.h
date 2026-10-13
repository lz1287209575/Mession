/**
 * @file ActorRoute.h
 * @brief ActorRoute - actor 路由条目与 actor 定位结果的 POD 定义。
 */

#pragma once

#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/MLib.h"

/**
 * @brief SActorRoute - 一条 actor 路由:某个 ActorId 在某个 endpoint 上的位置。
 *
 * @detail 一个 ActorId 可以有多条路由(分布在不同 ServerType / 不同连接),这正是
 *   MActorRouter 1:N fan-out 的基础。FindActor 只返回第一条(向后兼容),
 *   需要全量时用 MActorRouter::FindAllActorRoutes。
 */
struct SActorRoute {
    uint64      ActorId        = 0;                    ///< 全局 actor id;0 表示"查无此路由"(FindActor 的空结果按此判定)
    EServerType ServerType     = EServerType::Unknown; ///< 所在服务类型;Unknown 表示本进程本地 actor
    uint64      ConnectionId   = 0;                    ///< 远端场景为目标连接 id;本地 actor(ServerType==Unknown)存的是 Sub Reactor 的 SubId
    uint64      LastUpdateTime = 0;                    ///< 最后写入时刻 = MTime::GetTimeSeconds() * 1000(毫秒);当前只写不读
};

/**
 * @brief FLocateActorResult - MActorRouter::LocateActor 的查询结果。
 */
struct FLocateActorResult {
    bool        bFound       = false;                ///< 是否在路由表里找到该 actor
    EServerType ServerType   = EServerType::Unknown; ///< 命中时的服务类型(未命中保持 Unknown)
    uint64      ConnectionId = 0;                    ///< 命中时的连接 id(未命中保持 0)
    MString     TargetClassName;                     ///< 目标类名;当前实现从不写入,恒为空
    MString     MethodName;                          ///< 目标方法名;当前实现从不写入,恒为空
};
