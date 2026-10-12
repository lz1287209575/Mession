#include "Servers/Player/PlayerService.h"

#include "Common/Net/Rpc/MRpcChannel.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/Rpc/RpcTransport.h"
#include "Common/Net/ServiceDiscovery/Endpoint.h"
#include "Common/Net/ServiceDiscovery/EndpointCache.h"
#include "Common/Runtime/Actor/FActorMessage.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Id.h"
#include "Common/Runtime/Log/Log.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/Player/Members/MPlayerItemContainer.h"

#include <algorithm>
#include <thread>

MPlayerService* GGlobalPlayerService = nullptr;

MPlayerService* MPlayerService::GetSingleton() {
    return GGlobalPlayerService;
}

bool MPlayerService::Init(int InPort) {
    Config = MService<SPlayerServiceConfig>::GetConfig();

    const EServerType Derived = MServiceMain::ParseLocalServerType(Config.LocalServerTypeName);

    // 调试:定位 --local-type 解析(2026-08-20 临时,端到端验证后移除)
    LOG_INFO("MPlayerService: config local_type_name='%s' listen=%u derived=%d", Config.LocalServerTypeName.c_str(), static_cast<unsigned>(Config.ListenPort), static_cast<int>(Derived));
    if (Derived == EServerType::Unknown && !Config.LocalServerTypeName.empty()) {
        LOG_WARN("MPlayerService: --local-type=%s did not match any known server type; "
                 "leaving LocalServerType=Unknown",
                 Config.LocalServerTypeName.c_str());
    }
    Config.LocalServerType = Derived;

    if (InPort > 0) {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    if (Config.ListenPort == 0) {
        LOG_ERROR("MPlayerService: ListenPort is 0");
        return false;
    }

    if (Config.RegistryAddr.empty()) {
        LOG_ERROR("MPlayerService: --registry=<host:port> is required");
        return false;
    }

    if (Config.LocalServerId == 0) {
        Config.LocalServerId = static_cast<uint32>(MUniqueIdGenerator::Generate());
    }

    bRunning             = true;
    GGlobalPlayerService = this;

    CORE_LOG(Info, ":: Mession Game Server :: PlayerService (v%s)", "1.0.0");
    NET_LOG(Info, "Starting %s on port %u", Config.ServiceName.c_str(), static_cast<unsigned>(Config.ListenPort));

    MServerConnection::SetLocalInfo(Config.LocalServerId, Config.LocalServerType, Config.ServiceName.c_str());

    // 多 Reactor 装配 + actor 运行时绑定 SubPool
    const uint32 SubCount = Config.SubCount;
    if (SubCount > 0) {
        InitSubPool(SubCount);
        LOG_INFO("MPlayerService: InitSubPool(%u) enabled (multi-reactor)", static_cast<unsigned>(SubCount));
        for (uint32 SubId = 0; SubId < SubCount; ++SubId) {
            MEndpointCache::Get().SetServiceInstance(SubId, this);
        }
        MActorSystem::Get().Init(GetSubPool());
    }

    // 注册到 Registry
    MEndpointCache::Get().AttachEventLoop(&EventLoop);
    MEndpointCache::Get().SetServiceInstance(this);
    MString RegHost;
    uint16  RegPort = 0;
    if (!ParseAddrPort(Config.RegistryAddr, RegHost, RegPort)) {
        LOG_ERROR("MPlayerService: malformed --registry=%s (expected host:port)", Config.RegistryAddr.c_str());
        return false;
    }
    MEndpointCache::Get().BindRegistry(RegHost, RegPort);
    MEndpointCache::Get().RegisterLocal(MakeLocalEndpoint(Config));

    RegisterLocalActors();

    // 验证:demo 玩家 actor(PlayerId=10001)+ 挂载背包成员,注册进 MActorSystem。
    // 正式形态由 RegisterPlayer(登录/会话激活)驱动创建——PoC 固定一个演示。
    if (SubCount > 0) {
        const uint64             DemoPlayerId = 10001;
        TSharedPtr<MPlayerActor> DemoPlayer   = NewMObject<MPlayerActor>(nullptr, "DemoPlayer", DemoPlayerId);
        DemoPlayer->AddMember("MPlayerItemContainer", CreateDefaultSubObject<MPlayerItemContainer>(DemoPlayer.Get(), "ItemContainer"));
        MActorRouter::Get().RegisterActor(DemoPlayer, GetSubPool());
        PlayerActors.push_back(DemoPlayer);
        LOG_INFO("MPlayerService: demo player actor registered player=%llu (members: MPlayerItemContainer)", static_cast<unsigned long long>(DemoPlayerId));
    }

    return true;
}

void MPlayerService::TickBackends() {
    MEndpointCache::Get().Tick(0.1f);
}

uint16 MPlayerService::GetListenPort() const {
    return Config.ListenPort;
}

void MPlayerService::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) {
    DispatchConnection(
        ConnId, Conn,
        [this, Conn](uint64 /*Cid*/, const TByteArray& Payload) {
            if (Payload.empty()) {
                return;
            }
            const uint8 PacketType = Payload[0];
            TByteArray  Data(Payload.begin() + 1, Payload.end());
            if (static_cast<EServerMessageType>(PacketType) == EServerMessageType::MT_FunctionCall) {
                DispatchBackendServerCallPacketInbound(this, Conn, Data);
            } else if (static_cast<EServerMessageType>(PacketType) == EServerMessageType::MT_FunctionResponse) {
                HandleServerCallResponse(Data);
            }
        },
        [](uint64 /*Cid*/) {});
}

void MPlayerService::ShutdownConnections() {
    for (uint32 InstId : Config.LocalActorIds) {
        const uint64 ActorId = MServiceId::Make(Config.LocalServerType, InstId);
        MActorRouter::Get().UnregisterActor(ActorId);
    }
    PlayerActors.clear();
    MEndpointCache::Get().DeregisterAndShutdown();
}

void MPlayerService::OnRunStarted() {
    LOG_INFO("%s running on port %u", Config.ServiceName.c_str(), static_cast<unsigned>(Config.ListenPort));
}

void MPlayerService::RegisterLocalActors() {
    for (uint32 InstId : Config.LocalActorIds) {
        const uint64 ActorId = MServiceId::Make(Config.LocalServerType, InstId);
        MActorRouter::Get().RegisterActor(ActorId, EServerType::Unknown);
        LOG_INFO("%s: registered local actor ServiceId=%u InstId=%u (ActorId=%llu)", Config.ServiceName.c_str(), static_cast<unsigned>(MServiceId::GetServiceId(ActorId)), static_cast<unsigned>(MServiceId::GetInstId(ActorId)),
                 static_cast<unsigned long long>(ActorId));
    }
}

// 框架成员分发入口:直接转交框架分发器——查 GMemberRpcEntries → 请求反序列化
// 取 PlayerId → MActorSystem::Find(PlayerId) → actor 宿主成员表 → 成员反射调用。
// 这里是框架机制,不是业务协议;业务逻辑在成员(MPlayerItemContainer)上。
SFutureResult<TByteArray> MPlayerService::FrameworkMemberDispatch(const FMemberDispatchRequest& Req) {
    return DispatchFrameworkMemberCall(Req.FunctionId, Req.Payload);
}

SFutureResult<SEmptyServerMessage> MPlayerService::RegisterPlayer(uint64 PlayerId) {
    if (PlayerId == 0) {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>("player_id_required");
    }
    if (MActorSystem::Get().Find(PlayerId).GetActor() != nullptr) {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>("player_already_online");
    }

    TSharedPtr<MPlayerActor> Player = NewMObject<MPlayerActor>(nullptr, "Player", PlayerId);
    Player->AddMember("MPlayerItemContainer", CreateDefaultSubObject<MPlayerItemContainer>(Player.Get(), "ItemContainer"));
    MActorRouter::Get().RegisterActor(Player, GetSubPool());
    PlayerActors.push_back(Player);
    LOG_INFO("MPlayerService: player registered player=%llu (members: MPlayerItemContainer)", static_cast<unsigned long long>(PlayerId));
    return MServerCallAsyncSupport::MakeSuccessFuture(SEmptyServerMessage{});
}
