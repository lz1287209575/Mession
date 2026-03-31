#include "Servers/Gateway/GatewayServer.h"
#include "Common/Net/Rpc/ClientManifest.h"
#include "Common/Net/Rpc/RpcClientCall.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Runtime/Object/Object.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/App/ServerRpcSupport.h"

namespace
{
TSharedPtr<IClientResponseTarget> MakeGatewayClientResponseTarget(const TSharedPtr<INetConnection>& ClientConnection)
{
    return MakeShared<MClientResponseTarget>(
        [ClientConnection](uint64 /*ExpectedConnectionId*/) -> bool
        {
            return ClientConnection && ClientConnection->IsConnected();
        },
        [ClientConnection](uint64 /*ExpectedConnectionId*/, uint16 ResponseFunctionId, uint64 ResponseCallId, const TByteArray& ResponsePayload) -> bool
        {
            TByteArray Packet;
            if (!BuildClientCallPacket(ResponseFunctionId, ResponseCallId, ResponsePayload, Packet))
            {
                return false;
            }

            return ClientConnection->Send(Packet.data(), static_cast<uint32>(Packet.size()));
        });
}

bool WriteDefaultClientResponseProperty(
    const MProperty* Property,
    const FAppError& Error,
    MReflectArchive& Archive)
{
    if (!Property)
    {
        return false;
    }

    switch (Property->Type)
    {
    case EPropertyType::Bool:
    {
        bool Value = false;
        Archive << Value;
        return true;
    }
    case EPropertyType::UInt8:
    {
        uint8 Value = 0;
        Archive << Value;
        return true;
    }
    case EPropertyType::UInt16:
    {
        uint16 Value = 0;
        Archive << Value;
        return true;
    }
    case EPropertyType::UInt32:
    {
        uint32 Value = 0;
        Archive << Value;
        return true;
    }
    case EPropertyType::UInt64:
    {
        uint64 Value = 0;
        Archive << Value;
        return true;
    }
    case EPropertyType::Int8:
    {
        int8 Value = 0;
        Archive << Value;
        return true;
    }
    case EPropertyType::Int16:
    {
        int16 Value = 0;
        Archive << Value;
        return true;
    }
    case EPropertyType::Int32:
    {
        int32 Value = 0;
        Archive << Value;
        return true;
    }
    case EPropertyType::Int64:
    {
        int64 Value = 0;
        Archive << Value;
        return true;
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        Archive << Value;
        return true;
    }
    case EPropertyType::Double:
    {
        double Value = 0.0;
        Archive << Value;
        return true;
    }
    case EPropertyType::String:
    {
        MString Value;
        if (Property->Name == "Error")
        {
            Value = !Error.Code.empty() ? Error.Code : Error.Message;
        }
        Archive << Value;
        return true;
    }
    default:
        return false;
    }
}

bool BuildClientErrorResponsePayload(uint16 FunctionId, const FAppError& Error, TByteArray& OutPayload)
{
    MClass* ResponseStruct = FindGlobalClientResponseStructById(FunctionId);
    if (!ResponseStruct)
    {
        return false;
    }

    MReflectArchive Archive;
    for (const MProperty* Property : ResponseStruct->GetProperties())
    {
        if (!WriteDefaultClientResponseProperty(Property, Error, Archive))
        {
            LOG_WARN("Gateway cannot build reflected error response: function_id=%u property=%s",
                     static_cast<unsigned>(FunctionId),
                     Property ? Property->Name.c_str() : "<null>");
            return false;
        }
    }

    OutPayload = std::move(Archive.Data);
    return true;
}

bool DispatchBackendServerCallPacket(
    MObject* Service,
    const TSharedPtr<MServerConnection>& Connection,
    const TByteArray& Data)
{
    if (!Service || !Connection || Data.empty())
    {
        return false;
    }

    uint16 FunctionId = 0;
    uint64 CallId = 0;
    uint32 PayloadSize = 0;
    size_t PayloadOffset = 0;
    if (!ParseServerCallPacket(Data, FunctionId, CallId, PayloadSize, PayloadOffset))
    {
        return false;
    }

    TByteArray RequestPayload;
    if (PayloadSize > 0)
    {
        RequestPayload.insert(
            RequestPayload.end(),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    const TSharedPtr<IServerCallResponseTarget> ResponseTarget =
        MakeShared<MServerCallResponseTarget>(
            [Connection]() -> bool
            {
                return Connection && Connection->IsConnected();
            },
            [Connection](uint16 ResponseFunctionId, uint64 ResponseCallId, bool bSuccess, const TByteArray& ResponsePayload) -> bool
            {
                TByteArray ResponsePacketPayload;
                if (!BuildServerCallResponsePacket(
                        ResponseFunctionId,
                        ResponseCallId,
                        bSuccess,
                        ResponsePayload,
                        ResponsePacketPayload))
                {
                    return false;
                }

                return SendServerCallResponseMessage(Connection, ResponsePacketPayload);
            });

    return DispatchServerCall(Service, FunctionId, CallId, RequestPayload, ResponseTarget);
}

void SendClientErrorResponse(
    const TSharedPtr<IClientResponseTarget>& ResponseTarget,
    uint64 ConnectionId,
    uint16 FunctionId,
    uint64 CallId,
    const FAppError& Error)
{
    if (!ResponseTarget)
    {
        return;
    }

    TByteArray Payload;
    if (!BuildClientErrorResponsePayload(FunctionId, Error, Payload))
    {
        LOG_WARN("Gateway failed to materialize client error response: function_id=%u code=%s",
                 static_cast<unsigned>(FunctionId),
                 Error.Code.c_str());
        return;
    }

    (void)ResponseTarget->SendClientResponse(ConnectionId, FunctionId, CallId, Payload);
}
}

bool MGatewayServer::LoadConfig(const MString& /*ConfigPath*/)
{
    return true;
}

bool MGatewayServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    bRunning = true;
    MLogger::LogStartupBanner("GatewayServer", Config.ListenPort, 0);
    MServerConnection::SetLocalInfo(1, EServerType::Gateway, "GatewaySkeleton");

    WorldServerConn = BackendConnectionManager.AddServer(
        SServerConnectionConfig(3, EServerType::World, "WorldSkeleton", Config.WorldServerAddr, Config.WorldServerPort));

    WorldServerConn->SetOnMessage([this](auto Connection, uint8 PacketType, const TByteArray& Data)
    {
        HandleBackendPacket(Connection, PacketType, Data, "World");
    });

    WorldServerConn->Connect();

    RegisterRpcTransport(EServerType::World, WorldServerConn);

    return true;
}

void MGatewayServer::Tick()
{
}

uint16 MGatewayServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MGatewayServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    ClientConnections[ConnId] = Conn;
    LOG_INFO("Gateway skeleton accepted connection %llu", static_cast<unsigned long long>(ConnId));
    EventLoop.RegisterConnection(
        ConnId,
        Conn,
        [this](uint64 ConnectionId, const TByteArray& Payload)
        {
            HandleClientPacket(ConnectionId, Payload);
        },
        [this](uint64 ConnectionId)
        {
            ClientConnections.erase(ConnectionId);
        });
}

void MGatewayServer::TickBackends()
{
    BackendConnectionManager.Tick(0.1f);
}

void MGatewayServer::ShutdownConnections()
{
    for (auto& [ConnId, Conn] : ClientConnections)
    {
        (void)ConnId;
        if (Conn)
        {
            Conn->Close();
        }
    }
    ClientConnections.clear();
    BackendConnectionManager.DisconnectAll();
    ClearRpcTransports();
    WorldServerConn.reset();
}

void MGatewayServer::OnRunStarted()
{
    LOG_INFO("Gateway skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}

void MGatewayServer::Client_Echo(FClientEchoRequest& Request, FClientEchoResponse& Response)
{
    Response.ConnectionId = GetCurrentClientConnectionId();
    Response.Message = Request.Message;
}

void MGatewayServer::Client_Heartbeat(FClientHeartbeatRequest& Request, FClientHeartbeatResponse& Response)
{
    Response.bSuccess = true;
    Response.Sequence = Request.Sequence;
    Response.ConnectionId = GetCurrentClientConnectionId();
}

MFuture<TResult<SEmptyServerMessage, FAppError>> MGatewayServer::PushClientDownlink(
    const FClientDownlinkPushRequest& Request)
{
    if (Request.GatewayConnectionId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>(
            "gateway_connection_id_required",
            "PushClientDownlink");
    }

    if (Request.FunctionId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>(
            "client_downlink_function_required",
            "PushClientDownlink");
    }

    const auto It = ClientConnections.find(Request.GatewayConnectionId);
    if (It == ClientConnections.end() || !It->second || !It->second->IsConnected())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>(
            "gateway_client_connection_missing",
            "PushClientDownlink");
    }

    TByteArray Packet;
    if (!BuildClientFunctionPacket(Request.FunctionId, Request.Payload, Packet))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>(
            "client_downlink_packet_build_failed",
            "PushClientDownlink");
    }

    if (!It->second->Send(Packet.data(), static_cast<uint32>(Packet.size())))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>(
            "client_downlink_send_failed",
            "PushClientDownlink");
    }

    return MServerCallAsyncSupport::MakeSuccessFuture(SEmptyServerMessage{});
}

void MGatewayServer::HandleClientPacket(uint64 ConnectionId, const TByteArray& Data)
{
    uint16 FunctionId = 0;
    uint64 CallId = 0;
    uint32 PayloadSize = 0;
    size_t PayloadOffset = 0;
    if (!ParseClientCallPacket(Data, FunctionId, CallId, PayloadSize, PayloadOffset))
    {
        LOG_WARN("Gateway client packet parse failed: connection=%llu", static_cast<unsigned long long>(ConnectionId));
        return;
    }

    TByteArray Payload;
    if (PayloadSize > 0)
    {
        Payload.insert(
            Payload.end(),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    auto ConnIt = ClientConnections.find(ConnectionId);
    if (ConnIt == ClientConnections.end() || !ConnIt->second)
    {
        LOG_WARN("Gateway missing client connection for client call: connection=%llu",
                 static_cast<unsigned long long>(ConnectionId));
        return;
    }

    const TSharedPtr<INetConnection> ClientConnection = ConnIt->second;
    const TSharedPtr<IClientResponseTarget> ResponseTarget = MakeGatewayClientResponseTarget(ClientConnection);
    const MClientManifest::SEntry* ClientEntry = FindGlobalClientFunctionEntryById(FunctionId);
    if (!ClientEntry)
    {
        LOG_WARN("Gateway received unknown client function id=%u connection=%llu",
                 static_cast<unsigned>(FunctionId),
                 static_cast<unsigned long long>(ConnectionId));
        return;
    }

    const EServerType TargetServerType = GetGlobalClientFunctionTargetServerType(FunctionId);
    const bool bDispatchLocal =
        TargetServerType == EServerType::Gateway ||
        (ClientEntry->OwnerType && std::strcmp(ClientEntry->OwnerType, "MGatewayServer") == 0);

    if (bDispatchLocal)
    {
        const SClientDispatchOutcome Outcome =
            DispatchClientFunction(this, ConnectionId, FunctionId, CallId, Payload, ResponseTarget);
        if (Outcome.Result != EClientDispatchResult::Handled)
        {
            LOG_WARN("Gateway local client dispatch failed: function=%s result=%u",
                     Outcome.FunctionName ? Outcome.FunctionName : "<unknown>",
                     static_cast<unsigned>(Outcome.Result));
        }
        return;
    }

    const TSharedPtr<MServerConnection> TargetConnection = ResolveServerTransport(TargetServerType);
    if (!TargetConnection || !TargetConnection->IsConnected())
    {
        SendClientErrorResponse(
            ResponseTarget,
            ConnectionId,
            FunctionId,
            CallId,
            FAppError::Make("client_route_backend_unavailable", ClientEntry->FunctionName ? ClientEntry->FunctionName : ""));
        return;
    }

    FForwardedClientCallRequest ForwardRequest;
    ForwardRequest.GatewayConnectionId = ConnectionId;
    ForwardRequest.ClientFunctionId = FunctionId;
    ForwardRequest.ClientCallId = CallId;
    ForwardRequest.Payload = Payload;

    CallServerFunction<FForwardedClientCallResponse>(TargetConnection, TargetServerType, "ForwardClientCall", ForwardRequest)
        .Then(
            [ResponseTarget, ConnectionId, FunctionId, CallId](MFuture<TResult<FForwardedClientCallResponse, FAppError>> Completed) mutable
            {
                try
                {
                    const TResult<FForwardedClientCallResponse, FAppError> Result = Completed.Get();
                    if (Result.IsOk())
                    {
                        (void)ResponseTarget->SendClientResponse(
                            ConnectionId,
                            FunctionId,
                            CallId,
                            Result.GetValue().Payload);
                        return;
                    }

                    SendClientErrorResponse(ResponseTarget, ConnectionId, FunctionId, CallId, Result.GetError());
                }
                catch (const std::exception& Ex)
                {
                    SendClientErrorResponse(
                        ResponseTarget,
                        ConnectionId,
                        FunctionId,
                        CallId,
                        FAppError::Make("client_route_exception", Ex.what()));
                }
                catch (...)
                {
                    SendClientErrorResponse(
                        ResponseTarget,
                        ConnectionId,
                        FunctionId,
                        CallId,
                        FAppError::Make("client_route_exception", "unknown"));
                }
            });
}

void MGatewayServer::HandleBackendPacket(
    const TSharedPtr<MServerConnection>& Connection,
    uint8 PacketType,
    const TByteArray& Data,
    const char* PeerName)
{
    if (PacketType == static_cast<uint8>(EServerMessageType::MT_FunctionCall))
    {
        if (!DispatchBackendServerCallPacket(this, Connection, Data))
        {
            LOG_WARN("Gateway failed to dispatch backend function call from %s", PeerName ? PeerName : "backend");
        }
        return;
    }

    if (PacketType == static_cast<uint8>(EServerMessageType::MT_FunctionResponse))
    {
        if (!HandleServerCallResponse(Data))
        {
            LOG_WARN("Gateway failed to handle backend function response from %s", PeerName ? PeerName : "backend");
        }
        return;
    }

    LOG_WARN("Gateway received unsupported backend packet from %s: type=%u",
             PeerName ? PeerName : "backend",
             static_cast<unsigned>(PacketType));
}
