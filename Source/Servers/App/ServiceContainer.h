#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Net/ServerConnection.h"

// 进程内全局单例：持有本进程到其它 Service 进程的 MServerConnection transport map。
//
// 用途：任何跨进程 ServerCall 调用方先 Resolve(ServiceType) 拿到 transport，再 CallServerFunction。
//
// 不参与 MObject 反射树；不走 TSharedPtr 体系；进程启动时 RegisterAll() 一次性注入，
// 进程退出时由静态析构自动释放。
class MServiceContainer
{
public:
    static MServiceContainer& Get();

    // 注册一个 transport：建立连接 + 放到 map + 注册到 MServerRuntimeContext。
    // 通常在 ServiceMain::main() 启动阶段批量调用。
    void Register(const TSharedPtr<MServerConnection>& Conn);

    // 通过 EServerType 查 transport；返回 nullptr 表示未注册或连接未建立。
    TSharedPtr<MServerConnection> Resolve(EServerType ServerType) const;

    // 关闭所有 connection + 清空 map。在 ServiceMain 退出前调用。
    void ShutdownAll();

    // Tick 所有 connection。
    void TickAll(float DeltaTime);

private:
    MServiceContainer() = default;

    TMap<EServerType, TSharedPtr<MServerConnection>> Connections;
};