#pragma once

#include "NetDriver/Reflection.h"
#include "Common/ServerConnection.h"
#include "Core/Net/NetCore.h"

// ============================================
// 服务器间 RPC 辅助工具 + Service 定义
// 使用 MReflectObject / MFunction / ERpcType
// ============================================

// 构建跨服务器 RPC 载荷：
// Data 格式统一为：
//   [FunctionId(2)][PayloadSize(4)][Payload...]
// 其中 Payload 由 MReflectArchive 负责序列化参数。
bool BuildServerRpcPayload(uint16 FunctionId, const TArray& InPayload, TArray& OutData);

struct SRpcEndpointBinding
{
    EServerType ServerType = EServerType::Unknown;
    const char* ClassName = nullptr;
    const char* FunctionName = nullptr;
};

#define MDECLARE_RPC_ENDPOINT_BINDING(ClassNameLiteral, EndpointType, MethodName, Signature, FuncFlags, RpcKind, ReliableValue) \
    {EServerType::EndpointType, ClassNameLiteral, #MethodName},

#define MDECLARE_SERVICE_RPC(MethodName, Signature, FuncFlags, RpcKind, ReliableValue) \
    MFUNCTION(FuncFlags) void MethodName Signature; \
    using FHandler_##MethodName = TFunction<void Signature>; \
    static void SetHandler_##MethodName(const FHandler_##MethodName& InHandler); \
    static uint16 GetFunctionId_##MethodName();

// 基于共享端点描述构建 RPC 包。
// 这里使用稳定 FunctionId，因此发送端不需要依赖远端进程内的 StaticClass()。
bool BuildRpcPayloadForEndpoint(const SRpcEndpointBinding& Binding, const TArray& InPayload, TArray& OutData);

const SRpcEndpointBinding* FindRouterServerRegisterAckEndpoint(EServerType ServerType);
const SRpcEndpointBinding* FindRouterRouteResponseEndpoint(EServerType ServerType);

// 在接收端尝试解析并调用 Service 上的 RPC 函数。
// - ServiceInstance: 具体 Service 单例（如 MWorldService 实例）
// - Data: 原始 MT_RPC 包体（不含“消息类型”字节）
// - ExpectedType: 预期的 ERpcType（如 ERpcType::ServerToServer）
// 返回：
// - true  表示已成功解析并调用对应函数
// - false 表示数据格式非法或找不到匹配的函数（调用方可选择回退到旧逻辑）
bool TryInvokeServerRpc(MReflectObject* ServiceInstance, const TArray& Data, ERpcType ExpectedType);

#define MLOGIN_SERVER_ROUTER_ACK_RPC_LIST(OP) \
    OP("MLoginServer", Login, Rpc_OnRouterServerRegisterAck, (uint8 Result), NetServer, ServerToServer, true)

#define MGATEWAY_SERVER_ROUTER_ACK_RPC_LIST(OP) \
    OP("MGatewayServer", Gateway, Rpc_OnRouterServerRegisterAck, (uint8 Result), NetServer, ServerToServer, true)

#define MGATEWAY_SERVER_ROUTER_ROUTE_RPC_LIST(OP) \
    OP("MGatewayServer", Gateway, Rpc_OnRouterRouteResponse, (uint64 RequestId, uint8 RequestedTypeValue, uint64 PlayerId, bool bFound, uint32 ServerId, uint8 ServerTypeValue, const FString& ServerName, const FString& Address, uint16 Port, uint16 ZoneId), NetServer, ServerToServer, true)

#define MWORLD_SERVER_ROUTER_ACK_RPC_LIST(OP) \
    OP("MWorldServer", World, Rpc_OnRouterServerRegisterAck, (uint8 Result), NetServer, ServerToServer, true)

#define MWORLD_SERVER_ROUTER_ROUTE_RPC_LIST(OP) \
    OP("MWorldServer", World, Rpc_OnRouterRouteResponse, (uint64 RequestId, uint8 RequestedTypeValue, uint64 PlayerId, bool bFound, uint32 ServerId, uint8 ServerTypeValue, const FString& ServerName, const FString& Address, uint16 Port, uint16 ZoneId), NetServer, ServerToServer, true)

// ============================================
// 网关对外 Service：Login -> Gateway
// ============================================

class MGatewayService : public MReflectObject
{
public:
    MGENERATED_BODY(MGatewayService, MReflectObject, 0)

public:
    MDECLARE_SERVICE_RPC(
        Rpc_OnPlayerLoginResponse,
        (uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey),
        NetServer,
        ServerToServer,
        true)
};

// ============================================
// 登录服对外 Service：World -> Login
// ============================================

class MLoginService : public MReflectObject
{
public:
    MGENERATED_BODY(MLoginService, MReflectObject, 0)

public:
    MDECLARE_SERVICE_RPC(
        Rpc_OnPlayerLoginRequest,
        (uint64 ClientConnectionId, uint64 PlayerId),
        NetServer,
        ServerToServer,
        true)
    MDECLARE_SERVICE_RPC(
        Rpc_OnSessionValidateRequest,
        (uint64 ValidationRequestId, uint64 PlayerId, uint32 SessionKey),
        NetServer,
        ServerToServer,
        true)
};

// ============================================
// 世界服对外 Service：Login -> World
// ============================================

class MWorldService : public MReflectObject
{
public:
    MGENERATED_BODY(MWorldService, MReflectObject, 0)

public:
    // 会话校验结果回调（Login -> World）
    MDECLARE_SERVICE_RPC(
        Rpc_OnPlayerLoginRequest,
        (uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey),
        NetServer,
        ServerToServer,
        true)
    MDECLARE_SERVICE_RPC(
        Rpc_OnSessionValidateResponse,
        (uint64 ValidationRequestId, uint64 PlayerId, bool bValid),
        NetServer,
        ServerToServer,
        true)
};

#undef MDECLARE_SERVICE_RPC
