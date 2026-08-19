#include "Common/Net/ClientCall/MClientTargetResolver.h"

MClientTargetResolver& MClientTargetResolver::Get() {
    // Meyers 单例——首次访问时构造，进程内生命周期。
    // 与 MEndpointCache::Get() / MActorRouter::Get() 同模式。
    static MClientTargetResolver Instance;
    return Instance;
}

void MClientTargetResolver::RegisterConn(TSharedPtr<INetConnection> Conn) {
    if (!Conn) {
        return;
    }

    const uint64                Id = Conn->GetPlayerId();
    std::lock_guard<std::mutex> Lock(Mutex);
    Connections[Id] = std::move(Conn);
}

void MClientTargetResolver::UnregisterConn(TSharedPtr<INetConnection> Conn) {
    if (!Conn) {
        return;
    }

    const uint64                Id = Conn->GetPlayerId();
    std::lock_guard<std::mutex> Lock(Mutex);
    Connections.erase(Id);

    // 若被注销的连接正是当前绑定上下文，则清除绑定。
    // 用裸指针比较，避免通过绑定持有注销调用方的生命周期。
    if (CurrentContext.Get() == Conn.Get()) {
        CurrentContext.reset();
    }
}

void MClientTargetResolver::BindContext(TSharedPtr<INetConnection> Conn) {
    std::lock_guard<std::mutex> Lock(Mutex);
    CurrentContext = std::move(Conn);
}

void MClientTargetResolver::ClearContext() {
    std::lock_guard<std::mutex> Lock(Mutex);
    CurrentContext.reset();
}

TVector<TSharedPtr<INetConnection>> MClientTargetResolver::ResolveCurrentContext() {
    std::lock_guard<std::mutex> Lock(Mutex);

    TVector<TSharedPtr<INetConnection>> Result;
    if (CurrentContext && CurrentContext->IsConnected()) {
        Result.push_back(CurrentContext);
    }
    return Result;
}

TVector<TSharedPtr<INetConnection>> MClientTargetResolver::ResolveBroadcast() {
    std::lock_guard<std::mutex> Lock(Mutex);

    TVector<TSharedPtr<INetConnection>> Result;
    Result.reserve(Connections.size());
    for (const auto& Pair : Connections) {
        const TSharedPtr<INetConnection>& Conn = Pair.second;
        if (Conn && Conn->IsConnected()) {
            Result.push_back(Conn);
        }
    }
    return Result;
}
