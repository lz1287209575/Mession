#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct SPlayerLoginRequestMessage
{
    MPROPERTY()
    uint64 RequestId = 0;

    MPROPERTY()
    uint64 ConnectionId = 0;

    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct SPlayerLoginResponseMessage
{
    MPROPERTY()
    uint64 RequestId = 0;

    MPROPERTY()
    uint64 ConnectionId = 0;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SessionKey = 0;
};

MSTRUCT()
struct SSessionValidateRequestMessage
{
    MPROPERTY()
    uint64 ConnectionId = 0;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SessionKey = 0;
};

MSTRUCT()
struct SSessionValidateResponseMessage
{
    MPROPERTY()
    uint64 ConnectionId = 0;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    bool bValid = false;
};

MSTRUCT()
struct SPlayerLogoutMessage
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct SPlayerIdPayload
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FLoginIssueSessionRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 GatewayConnectionId = 0;
};

MSTRUCT()
struct FLoginIssueSessionResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SessionKey = 0;
};

MSTRUCT()
struct FLoginValidateSessionRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SessionKey = 0;
};

MSTRUCT()
struct FLoginValidateSessionResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    bool bValid = false;
};
