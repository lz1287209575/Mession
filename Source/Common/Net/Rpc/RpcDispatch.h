#pragma once

#include "Common/Net/NetMessages.h"
#include "Common/Runtime/Reflect/Reflection.h"

#include <utility>

struct SClientRouteRequest
{
    enum class ERouteKind : uint8
    {
        None = 0,
        Login = 1,
        World = 2,
        RouterResolved = 3,
    };

    uint64 ConnectionId = 0;
    EClientMessageType MessageType = EClientMessageType::MT_Error;
    const char* FunctionName = nullptr;
    ERouteKind RouteKind = ERouteKind::None;
    const char* RouteName = nullptr;
    EServerType TargetServerType = EServerType::Unknown;
    const char* TargetName = nullptr;
    const char* AuthMode = nullptr;
    const char* WrapMode = nullptr;
    const TByteArray* Payload = nullptr;
};

enum class EClientDispatchResult : uint8
{
    NotFound = 0,
    Routed = 1,
    Handled = 2,
    RouteTargetUnsupported = 3,
    MissingFunction = 4,
    MissingBinder = 5,
    ParamBindingFailed = 6,
    InvokeFailed = 7,
    AuthRequired = 8,
    RoutePending = 9,
    BackendUnavailable = 10,
};

struct SClientDispatchOutcome
{
    EClientDispatchResult Result = EClientDispatchResult::NotFound;
    const char* OwnerType = nullptr;
    const char* FunctionName = nullptr;
};

class IClientRouteTarget
{
public:
    virtual ~IClientRouteTarget() = default;
    virtual EClientDispatchResult HandleClientRoute(const SClientRouteRequest& Request) = 0;
};

class IClientResponseTarget
{
public:
    virtual ~IClientResponseTarget() = default;
    virtual bool CanSendClientResponse(uint64 ConnectionId) const = 0;
    virtual bool SendClientResponse(uint64 ConnectionId, uint16 FunctionId, uint64 CallId, const TByteArray& Payload) = 0;
};

class MClientResponseTarget final : public IClientResponseTarget
{
public:
    MClientResponseTarget(
        TFunction<bool(uint64)> InCanSend,
        TFunction<bool(uint64, uint16, uint64, const TByteArray&)> InSend)
        : CanSendCallback(std::move(InCanSend))
        , SendCallback(std::move(InSend))
    {
    }

    bool CanSendClientResponse(uint64 ConnectionId) const override
    {
        return CanSendCallback ? CanSendCallback(ConnectionId) : false;
    }

    bool SendClientResponse(uint64 ConnectionId, uint16 FunctionId, uint64 CallId, const TByteArray& Payload) override
    {
        if (!CanSendClientResponse(ConnectionId))
        {
            return false;
        }
        return SendCallback ? SendCallback(ConnectionId, FunctionId, CallId, Payload) : false;
    }

private:
    TFunction<bool(uint64)> CanSendCallback;
    TFunction<bool(uint64, uint16, uint64, const TByteArray&)> SendCallback;
};

bool TryDispatchClientMessage(
    MObject* TargetInstance,
    uint64 ConnectionId,
    EClientMessageType MessageType,
    const TByteArray& Payload);
SClientDispatchOutcome DispatchClientMessage(
    MObject* TargetInstance,
    uint64 ConnectionId,
    EClientMessageType MessageType,
    const TByteArray& Payload);
SClientDispatchOutcome DispatchClientFunction(
    MObject* TargetInstance,
    uint64 ConnectionId,
    uint16 FunctionId,
    uint64 CallId,
    const TByteArray& Payload);
SClientDispatchOutcome DispatchClientFunction(
    MObject* TargetInstance,
    uint64 ConnectionId,
    uint16 FunctionId,
    uint64 CallId,
    const TByteArray& Payload,
    const TSharedPtr<IClientResponseTarget>& ResponseTarget);
const MFunction* FindClientFunctionById(const MClass* TargetClass, uint16 FunctionId);
