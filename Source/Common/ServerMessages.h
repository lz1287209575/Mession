#pragma once

#include "Common/MessageUtils.h"
#include "Common/ServerConnection.h"
#include "Common/StringUtils.h"
#include "Common/Logger.h"

struct SEmptyServerMessage
{
};

inline void Serialize(MMessageWriter& /*Writer*/, const SEmptyServerMessage& /*Message*/)
{
}

inline bool Deserialize(MMessageReader& Reader, SEmptyServerMessage& /*OutMessage*/)
{
    return Reader.GetRemainingSize() == 0;
}

struct SServerHandshakeMessage
{
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    FString ServerName;
};

inline void Serialize(MMessageWriter& Writer, const SServerHandshakeMessage& Message)
{
    Writer.Write(Message.ServerId)
        .Write(static_cast<uint8>(Message.ServerType))
        .WriteString(Message.ServerName);
}

inline bool Deserialize(MMessageReader& Reader, SServerHandshakeMessage& OutMessage)
{
    uint8 ServerTypeValue = 0;
    if (!Reader.Read(OutMessage.ServerId) ||
        !Reader.Read(ServerTypeValue) ||
        !Reader.ReadString(OutMessage.ServerName))
    {
        return false;
    }

    OutMessage.ServerType = static_cast<EServerType>(ServerTypeValue);
    return true;
}

struct SServerRegisterMessage
{
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    FString ServerName;
    FString Address;
    uint16 Port = 0;
    uint16 ZoneId = 0;
};

inline void Serialize(MMessageWriter& Writer, const SServerRegisterMessage& Message)
{
    Writer.Write(Message.ServerId)
        .Write(static_cast<uint8>(Message.ServerType))
        .WriteString(Message.ServerName)
        .WriteString(Message.Address)
        .Write(Message.Port)
        .Write(Message.ZoneId);
}

inline bool Deserialize(MMessageReader& Reader, SServerRegisterMessage& OutMessage)
{
    uint8 ServerTypeValue = 0;
    if (!Reader.Read(OutMessage.ServerId) ||
        !Reader.Read(ServerTypeValue) ||
        !Reader.ReadString(OutMessage.ServerName) ||
        !Reader.ReadString(OutMessage.Address) ||
        !Reader.Read(OutMessage.Port))
    {
        return false;
    }

    OutMessage.ServerType = static_cast<EServerType>(ServerTypeValue);
    if (Reader.GetRemainingSize() >= sizeof(uint16))
    {
        Reader.Read(OutMessage.ZoneId);
    }
    return true;
}

struct SServerRegisterAckMessage
{
    uint8 Result = 0;
};

inline void Serialize(MMessageWriter& Writer, const SServerRegisterAckMessage& Message)
{
    Writer.Write(Message.Result);
}

inline bool Deserialize(MMessageReader& Reader, SServerRegisterAckMessage& OutMessage)
{
    return Reader.Read(OutMessage.Result);
}

struct SServerLoadReportMessage
{
    uint32 CurrentLoad = 0;
    uint32 Capacity = 0;
};

inline void Serialize(MMessageWriter& Writer, const SServerLoadReportMessage& Message)
{
    Writer.Write(Message.CurrentLoad).Write(Message.Capacity);
}

inline bool Deserialize(MMessageReader& Reader, SServerLoadReportMessage& OutMessage)
{
    return Reader.Read(OutMessage.CurrentLoad) && Reader.Read(OutMessage.Capacity);
}

struct SRouteQueryMessage
{
    uint64 RequestId = 0;
    EServerType RequestedType = EServerType::Unknown;
    uint64 PlayerId = 0;
    uint16 ZoneId = 0;
};

inline void Serialize(MMessageWriter& Writer, const SRouteQueryMessage& Message)
{
    Writer.Write(Message.RequestId)
        .Write(static_cast<uint8>(Message.RequestedType))
        .Write(Message.PlayerId)
        .Write(Message.ZoneId);
}

inline bool Deserialize(MMessageReader& Reader, SRouteQueryMessage& OutMessage)
{
    uint8 RequestedTypeValue = 0;
    if (!Reader.Read(OutMessage.RequestId) ||
        !Reader.Read(RequestedTypeValue) ||
        !Reader.Read(OutMessage.PlayerId))
    {
        return false;
    }

    OutMessage.RequestedType = static_cast<EServerType>(RequestedTypeValue);
    if (Reader.GetRemainingSize() >= sizeof(uint16))
    {
        Reader.Read(OutMessage.ZoneId);
    }
    return true;
}

struct SRouteResponseMessage
{
    uint64 RequestId = 0;
    EServerType RequestedType = EServerType::Unknown;
    uint64 PlayerId = 0;
    bool bFound = false;
    SServerInfo ServerInfo;
};

inline void Serialize(MMessageWriter& Writer, const SRouteResponseMessage& Message)
{
    Writer.Write(Message.RequestId)
        .Write(static_cast<uint8>(Message.RequestedType))
        .Write(Message.PlayerId)
        .Write(static_cast<uint8>(Message.bFound ? 1 : 0));

    if (!Message.bFound)
    {
        return;
    }

    Writer.Write(Message.ServerInfo.ServerId)
        .Write(static_cast<uint8>(Message.ServerInfo.ServerType))
        .WriteString(Message.ServerInfo.ServerName)
        .WriteString(Message.ServerInfo.Address)
        .Write(Message.ServerInfo.Port)
        .Write(Message.ServerInfo.ZoneId);
}

inline bool Deserialize(MMessageReader& Reader, SRouteResponseMessage& OutMessage)
{
    uint8 RequestedTypeValue = 0;
    uint8 FoundValue = 0;
    if (!Reader.Read(OutMessage.RequestId) ||
        !Reader.Read(RequestedTypeValue) ||
        !Reader.Read(OutMessage.PlayerId) ||
        !Reader.Read(FoundValue))
    {
        return false;
    }

    OutMessage.RequestedType = static_cast<EServerType>(RequestedTypeValue);
    OutMessage.bFound = (FoundValue != 0);
    if (!OutMessage.bFound)
    {
        return true;
    }

    uint8 ServerTypeValue = 0;
    if (!Reader.Read(OutMessage.ServerInfo.ServerId) ||
        !Reader.Read(ServerTypeValue) ||
        !Reader.ReadString(OutMessage.ServerInfo.ServerName) ||
        !Reader.ReadString(OutMessage.ServerInfo.Address) ||
        !Reader.Read(OutMessage.ServerInfo.Port))
    {
        return false;
    }

    OutMessage.ServerInfo.ServerType = static_cast<EServerType>(ServerTypeValue);
    if (Reader.GetRemainingSize() >= sizeof(uint16))
    {
        Reader.Read(OutMessage.ServerInfo.ZoneId);
    }
    return true;
}

struct SPlayerLoginRequestMessage
{
    uint64 ConnectionId = 0;
    uint64 PlayerId = 0;
};

inline void Serialize(MMessageWriter& Writer, const SPlayerLoginRequestMessage& Message)
{
    Writer.Write(Message.ConnectionId)
        .Write(Message.PlayerId);
}

inline bool Deserialize(MMessageReader& Reader, SPlayerLoginRequestMessage& OutMessage)
{
    return Reader.Read(OutMessage.ConnectionId) &&
           Reader.Read(OutMessage.PlayerId);
}

struct SPlayerLoginResponseMessage
{
    uint64 ConnectionId = 0;
    uint64 PlayerId = 0;
    uint32 SessionKey = 0;
};

inline void Serialize(MMessageWriter& Writer, const SPlayerLoginResponseMessage& Message)
{
    Writer.Write(Message.ConnectionId)
        .Write(Message.PlayerId)
        .Write(Message.SessionKey);
}

inline bool Deserialize(MMessageReader& Reader, SPlayerLoginResponseMessage& OutMessage)
{
    return Reader.Read(OutMessage.ConnectionId) &&
           Reader.Read(OutMessage.PlayerId) &&
           Reader.Read(OutMessage.SessionKey);
}

struct SSessionValidateRequestMessage
{
    uint64 ConnectionId = 0;
    uint64 PlayerId = 0;
    uint32 SessionKey = 0;
};

inline void Serialize(MMessageWriter& Writer, const SSessionValidateRequestMessage& Message)
{
    Writer.Write(Message.ConnectionId)
        .Write(Message.PlayerId)
        .Write(Message.SessionKey);
}

inline bool Deserialize(MMessageReader& Reader, SSessionValidateRequestMessage& OutMessage)
{
    return Reader.Read(OutMessage.ConnectionId) &&
           Reader.Read(OutMessage.PlayerId) &&
           Reader.Read(OutMessage.SessionKey);
}

struct SSessionValidateResponseMessage
{
    uint64 ConnectionId = 0;
    uint64 PlayerId = 0;
    bool bValid = false;
};

inline void Serialize(MMessageWriter& Writer, const SSessionValidateResponseMessage& Message)
{
    Writer.Write(Message.ConnectionId)
        .Write(Message.PlayerId)
        .Write(static_cast<uint8>(Message.bValid ? 1 : 0));
}

inline bool Deserialize(MMessageReader& Reader, SSessionValidateResponseMessage& OutMessage)
{
    uint8 bValidValue = 0;
    if (!Reader.Read(OutMessage.ConnectionId) ||
        !Reader.Read(OutMessage.PlayerId) ||
        !Reader.Read(bValidValue))
    {
        return false;
    }

    OutMessage.bValid = (bValidValue != 0);
    return true;
}

struct SPlayerSceneStateMessage
{
    uint64 PlayerId = 0;
    uint16 SceneId = 0;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

inline void Serialize(MMessageWriter& Writer, const SPlayerSceneStateMessage& Message)
{
    Writer.Write(Message.PlayerId)
        .Write(Message.SceneId)
        .Write(Message.X)
        .Write(Message.Y)
        .Write(Message.Z);
}

inline bool Deserialize(MMessageReader& Reader, SPlayerSceneStateMessage& OutMessage)
{
    return Reader.Read(OutMessage.PlayerId) &&
           Reader.Read(OutMessage.SceneId) &&
           Reader.Read(OutMessage.X) &&
           Reader.Read(OutMessage.Y) &&
           Reader.Read(OutMessage.Z);
}

struct SPlayerSceneLeaveMessage
{
    uint64 PlayerId = 0;
    uint16 SceneId = 0;
};

inline void Serialize(MMessageWriter& Writer, const SPlayerSceneLeaveMessage& Message)
{
    Writer.Write(Message.PlayerId)
        .Write(Message.SceneId);
}

inline bool Deserialize(MMessageReader& Reader, SPlayerSceneLeaveMessage& OutMessage)
{
    return Reader.Read(OutMessage.PlayerId) &&
           Reader.Read(OutMessage.SceneId);
}

struct SPlayerLogoutMessage
{
    uint64 PlayerId = 0;
};

inline void Serialize(MMessageWriter& Writer, const SPlayerLogoutMessage& Message)
{
    Writer.Write(Message.PlayerId);
}

inline bool Deserialize(MMessageReader& Reader, SPlayerLogoutMessage& OutMessage)
{
    return Reader.Read(OutMessage.PlayerId);
}

struct SHeartbeatMessage
{
    uint32 Sequence = 0;
};

inline void Serialize(MMessageWriter& Writer, const SHeartbeatMessage& Message)
{
    Writer.Write(Message.Sequence);
}

inline bool Deserialize(MMessageReader& Reader, SHeartbeatMessage& OutMessage)
{
    return Reader.Read(OutMessage.Sequence);
}

struct SChatMessage
{
    uint64 FromPlayerId = 0;
    FString Message;
};

inline void Serialize(MMessageWriter& Writer, const SChatMessage& InMessage)
{
    Writer.Write(InMessage.FromPlayerId)
        .WriteString(InMessage.Message);
}

inline bool Deserialize(MMessageReader& Reader, SChatMessage& OutMessage)
{
    return Reader.Read(OutMessage.FromPlayerId) &&
           Reader.ReadString(OutMessage.Message);
}

struct SGameplaySyncMessage
{
    uint64 ConnectionId = 0;
    TArray Data;
};

inline void Serialize(MMessageWriter& Writer, const SGameplaySyncMessage& Message)
{
    Writer.Write(Message.ConnectionId)
        .Write(static_cast<uint32>(Message.Data.size()))
        .WriteBytes(Message.Data);
}

inline bool Deserialize(MMessageReader& Reader, SGameplaySyncMessage& OutMessage)
{
    uint32 DataSize = 0;
    if (!Reader.Read(OutMessage.ConnectionId) ||
        !Reader.Read(DataSize) ||
        !Reader.ReadBytes(DataSize, OutMessage.Data))
    {
        return false;
    }

    return true;
}

struct SPlayerClientSyncMessage
{
    uint64 PlayerId = 0;
    TArray Data;
};

inline void Serialize(MMessageWriter& Writer, const SPlayerClientSyncMessage& Message)
{
    Writer.Write(Message.PlayerId)
        .Write(static_cast<uint32>(Message.Data.size()))
        .WriteBytes(Message.Data);
}

inline bool Deserialize(MMessageReader& Reader, SPlayerClientSyncMessage& OutMessage)
{
    uint32 DataSize = 0;
    if (!Reader.Read(OutMessage.PlayerId) ||
        !Reader.Read(DataSize) ||
        !Reader.ReadBytes(DataSize, OutMessage.Data))
    {
        return false;
    }

    return true;
}

// 仅含 PlayerId 的载荷（客户端登录请求、最小握手等）
struct SPlayerIdPayload
{
    uint64 PlayerId = 0;
};

inline void Serialize(MMessageWriter& Writer, const SPlayerIdPayload& Message)
{
    Writer.Write(Message.PlayerId);
}

inline bool Deserialize(MMessageReader& Reader, SPlayerIdPayload& OutMessage)
{
    return Reader.Read(OutMessage.PlayerId);
}

// 客户端登录响应载荷：SessionKey + PlayerId（Gateway 下发给客户端）
struct SClientLoginResponsePayload
{
    uint32 SessionKey = 0;
    uint64 PlayerId = 0;
};

inline void Serialize(MMessageWriter& Writer, const SClientLoginResponsePayload& Message)
{
    Writer.Write(Message.SessionKey).Write(Message.PlayerId);
}

inline bool Deserialize(MMessageReader& Reader, SClientLoginResponsePayload& OutMessage)
{
    return Reader.Read(OutMessage.SessionKey) && Reader.Read(OutMessage.PlayerId);
}

// 客户端移动载荷：位置 XYZ（World 解析 Gateway 转发的 gameplay 包）
struct SPlayerMovePayload
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

inline void Serialize(MMessageWriter& Writer, const SPlayerMovePayload& Message)
{
    Writer.Write(Message.X).Write(Message.Y).Write(Message.Z);
}

inline bool Deserialize(MMessageReader& Reader, SPlayerMovePayload& OutMessage)
{
    return Reader.Read(OutMessage.X) && Reader.Read(OutMessage.Y) && Reader.Read(OutMessage.Z);
}

template<typename TMessage>
inline TArray BuildPayload(const TMessage& Message)
{
    MMessageWriter Writer;
    Serialize(Writer, Message);
    return Writer.MoveData();
}

// 解析结果：成功为 Ok()，失败为 Err(描述信息)。Context 用于日志上下文（如消息类型名）。
template<typename TMessage>
inline TResult<void, FString> ParsePayload(const TArray& Data, TMessage& OutMessage, const char* Context = nullptr)
{
    MMessageReader Reader(Data);
    const size_t PayloadSize = Data.size();

    if (!Deserialize(Reader, OutMessage))
    {
        FString Err = (Context && Context[0]) ? (FString(Context) + ": ") : FString();
        Err += "payload_size=" + MString::ToString(static_cast<uint64>(PayloadSize)) + ", deserialize_failed";
        return TResult<void, FString>::Err(std::move(Err));
    }
    if (!Reader.IsValid())
    {
        FString Err = (Context && Context[0]) ? (FString(Context) + ": ") : FString();
        Err += "payload_size=" + MString::ToString(static_cast<uint64>(PayloadSize)) + ", read_overflow";
        return TResult<void, FString>::Err(std::move(Err));
    }
    const size_t Trailing = Reader.GetRemainingSize();
    if (Trailing != 0)
    {
        FString Err = (Context && Context[0]) ? (FString(Context) + ": ") : FString();
        Err += "payload_size=" + MString::ToString(static_cast<uint64>(PayloadSize)) + ", trailing_bytes=" + MString::ToString(static_cast<uint64>(Trailing));
        return TResult<void, FString>::Err(std::move(Err));
    }
    return TResult<void, FString>::Ok();
}

template<typename TMessage>
inline bool SendTypedServerMessage(MServerConnection& Connection, EServerMessageType Type, const TMessage& Message)
{
    TArray Payload = BuildPayload(Message);
    const uint8* PayloadData = Payload.empty() ? nullptr : Payload.data();
    return Connection.Send(static_cast<uint8>(Type), PayloadData, static_cast<uint32>(Payload.size()));
}

template<typename TMessage>
inline bool SendTypedServerMessage(const TSharedPtr<MServerConnection>& Connection, EServerMessageType Type, const TMessage& Message)
{
    if (!Connection)
    {
        return false;
    }

    return SendTypedServerMessage(*Connection, Type, Message);
}

// ============================================
// 通用服务器消息分发器（基于类型的“RPC”调用）
// ============================================

class MServerMessageDispatcher
{
public:
    using FHandler = TFunction<void(const TArray&)>;

private:
    TMap<uint8, FHandler> Handlers;

public:
    MServerMessageDispatcher() = default;

    template<typename TObject, typename TMessage>
    void Register(EServerMessageType Type, TObject* Object, void (TObject::*MemberFunc)(const TMessage&), const char* Context)
    {
        if (!Object || !MemberFunc)
        {
            return;
        }

        const uint8 TypeValue = static_cast<uint8>(Type);

        Handlers[TypeValue] = [Object, MemberFunc, Context, Type](const TArray& Data)
        {
            TMessage Message;
            auto ParseResult = ParsePayload(Data, Message, Context);
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ServerMessageDispatcher ParsePayload failed for type %d: %s",
                         static_cast<int>(Type),
                         ParseResult.GetError().c_str());
                return;
            }

            (Object->*MemberFunc)(Message);
        };
    }

    void Dispatch(uint8 Type, const TArray& Data) const
    {
        auto It = Handlers.find(Type);
        if (It == Handlers.end())
        {
            LOG_DEBUG("ServerMessageDispatcher has no handler for type %u",
                      static_cast<unsigned>(Type));
            return;
        }

        It->second(Data);
    }
};
