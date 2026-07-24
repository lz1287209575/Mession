#include "Common/Net/ClientCall/MClientTargetResolver.h"

MClientTargetResolver& MClientTargetResolver::Get()
{
    // Meyers singleton — first-touch construction, process-local lifetime.
    // Matches the pattern used by MEndpointCache::Get() and MActorRouter::Get().
    static MClientTargetResolver Instance;
    return Instance;
}

void MClientTargetResolver::RegisterConn(TSharedPtr<INetConnection> Conn)
{
    if (!Conn)
    {
        return;
    }

    const uint64 Id = Conn->GetPlayerId();
    std::lock_guard<std::mutex> Lock(Mutex);
    Connections[Id] = std::move(Conn);
}

void MClientTargetResolver::UnregisterConn(TSharedPtr<INetConnection> Conn)
{
    if (!Conn)
    {
        return;
    }

    const uint64 Id = Conn->GetPlayerId();
    std::lock_guard<std::mutex> Lock(Mutex);
    Connections.erase(Id);

    // If the unregistered connection was the current bind context, drop it.
    // Compare by raw pointer to avoid holding the unregistering caller alive
    // through the binding.
    if (CurrentContext.Get() == Conn.Get())
    {
        CurrentContext.reset();
    }
}

void MClientTargetResolver::BindContext(TSharedPtr<INetConnection> Conn)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    CurrentContext = std::move(Conn);
}

void MClientTargetResolver::ClearContext()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    CurrentContext.reset();
}

TVector<TSharedPtr<INetConnection>> MClientTargetResolver::ResolveCurrentContext()
{
    std::lock_guard<std::mutex> Lock(Mutex);

    TVector<TSharedPtr<INetConnection>> Result;
    if (CurrentContext && CurrentContext->IsConnected())
    {
        Result.push_back(CurrentContext);
    }
    return Result;
}

TVector<TSharedPtr<INetConnection>> MClientTargetResolver::ResolveBroadcast()
{
    std::lock_guard<std::mutex> Lock(Mutex);

    TVector<TSharedPtr<INetConnection>> Result;
    Result.reserve(Connections.size());
    for (const auto& Pair : Connections)
    {
        const TSharedPtr<INetConnection>& Conn = Pair.second;
        if (Conn && Conn->IsConnected())
        {
            Result.push_back(Conn);
        }
    }
    return Result;
}
