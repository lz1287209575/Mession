#include "Servers/App/ServiceContainer.h"
#include "Common/Net/Rpc/RpcManifest.h"
#include "Common/Runtime/Log/Logger.h"

MServiceContainer& MServiceContainer::Get()
{
    static MServiceContainer Instance;
    return Instance;
}

void MServiceContainer::Register(const TSharedPtr<MServerConnection>& Conn)
{
    if (!Conn)
    {
        return;
    }
    const EServerType ServerType = Conn->GetConfig().ServerType;
    Connections[ServerType] = Conn;
    LOG_INFO("ServiceContainer: registered peer %s (%s:%u)",
             GetServerTypeDisplayName(ServerType),
             Conn->GetConfig().Address.c_str(),
             static_cast<unsigned>(Conn->GetConfig().Port));
}

TSharedPtr<MServerConnection> MServiceContainer::Resolve(EServerType ServerType) const
{
    auto It = Connections.find(ServerType);
    return (It != Connections.end()) ? It->second : nullptr;
}

void MServiceContainer::ShutdownAll()
{
    for (auto& [Type, Conn] : Connections)
    {
        (void)Type;
        if (Conn)
        {
            Conn->Disconnect();
        }
    }
    Connections.clear();
}

void MServiceContainer::TickAll(float DeltaTime)
{
    for (auto& [Type, Conn] : Connections)
    {
        (void)Type;
        if (Conn)
        {
            Conn->Tick(DeltaTime);
        }
    }
}