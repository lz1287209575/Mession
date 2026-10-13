/**
 * @file MClientTargetContextGuard.h
 * @brief MClientTargetContextGuard - 绑定"当前客户端目标连接"的 RAII 守卫。
 *
 * @detail 用途:让非 Broadcast 的 CallClient 知道该推给哪个连接。守卫存活期间,任何
 *   MFUNCTION(Async, CallClient) 都会经 MClientTargetResolver::ResolveCurrentContext()
 *   解析到构造时传入的那个连接。
 *
 *   用法:
 *     {
 *         MClientTargetContextGuard Guard(Conn);
 *         // ... 调业务 API;期间的 CallClient 都落到 Conn
 *     } // <- 析构时调用 ClearContext()
 *
 *   注意:
 *     - 不可拷贝、不可移动:绑定与解绑严格配对在守卫的生命周期内。
 *     - 同一进程同一时刻只应有一个守卫生效。嵌套构造会后者覆盖前者(last writer wins),
 *       且内层守卫先析构时会把绑定整个清空 —— 外层随后发出的 CallClient 将解析不到目标。
 *       需要嵌套场景请显式控制作用域。
 *     - 守卫不持有连接所有权,只持有一份 TSharedPtr 引用。
 */

#pragma once

#include "Common/Net/ClientCall/MClientTargetResolver.h"

/**
 * @brief MClientTargetContextGuard - 构造时 BindContext,析构时 ClearContext。
 */
class MClientTargetContextGuard {
    public:
    /** @brief 构造即绑定:把 Conn 设为当前目标(传 nullptr 等价于清空绑定)。 */
    explicit MClientTargetContextGuard(TSharedPtr<INetConnection> Conn) {
        MClientTargetResolver::Get().BindContext(std::move(Conn));
    }

    /** @brief 析构即解绑:无条件 ClearContext()。 */
    ~MClientTargetContextGuard() {
        MClientTargetResolver::Get().ClearContext();
    }

    // 禁拷贝 / 禁移动:复制守卫会让"绑定 vs 解绑"的配对关系失效。
    MClientTargetContextGuard(const MClientTargetContextGuard&)            = delete;
    MClientTargetContextGuard& operator=(const MClientTargetContextGuard&) = delete;
    MClientTargetContextGuard(MClientTargetContextGuard&&)                 = delete;
    MClientTargetContextGuard& operator=(MClientTargetContextGuard&&)      = delete;
};
