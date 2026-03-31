# UE 客户端骨架设计稿

本文档给 UE 侧一套最小可落地的 C++ 类骨架设计，目标是快速搭起 `Mession` 登录链路。

适用日期：`2026-03-31`

说明：

- 这里给的是“设计稿 + 可直接抄的头文件草案”
- 不依赖当前仓库存在 UE 工程
- 不保证直接复制后 100% 编译通过
- 重点是帮助 UE Agent 快速建立合理的类边界

## 1. 推荐目录

建议 UE 工程里新建模块或目录：

```text
Source/<YourGame>/Mession/
    MessionTypes.h
    MessionByteCodec.h
    MessionByteCodec.cpp
    MessionGatewayProtocol.h
    MessionGatewayProtocol.cpp
    MessionTcpClient.h
    MessionTcpClient.cpp
    MessionLoginSubsystem.h
    MessionLoginSubsystem.cpp
```

如果想先更轻量，也可以都放进现有 Runtime 模块里，但建议至少保留这 5 个逻辑单元。

## 2. 类型定义

建议先有一个公共类型头，避免协议字段散落。

### `MessionTypes.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MessionTypes.generated.h"

USTRUCT(BlueprintType)
struct FMessionLoginRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint64 PlayerId = 0;
};

USTRUCT(BlueprintType)
struct FMessionLoginResponse
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    bool bSuccess = false;

    UPROPERTY(BlueprintReadOnly)
    uint64 PlayerId = 0;

    UPROPERTY(BlueprintReadOnly)
    uint32 SessionKey = 0;

    UPROPERTY(BlueprintReadOnly)
    FString Error;
};

USTRUCT(BlueprintType)
struct FMessionFindPlayerRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint64 PlayerId = 0;
};

USTRUCT(BlueprintType)
struct FMessionFindPlayerResponse
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    bool bFound = false;

    UPROPERTY(BlueprintReadOnly)
    uint64 PlayerId = 0;

    UPROPERTY(BlueprintReadOnly)
    uint64 GatewayConnectionId = 0;

    UPROPERTY(BlueprintReadOnly)
    uint32 SceneId = 0;

    UPROPERTY(BlueprintReadOnly)
    FString Error;
};

UENUM(BlueprintType)
enum class EMessionConnectionState : uint8
{
    Disconnected,
    Connecting,
    Connected
};

USTRUCT()
struct FMessionFunctionCallPacket
{
    GENERATED_BODY()

    uint8 MsgType = 13;
    uint16 FunctionId = 0;
    uint64 CallId = 0;
    TArray<uint8> Payload;
};
```

## 3. 字节编解码器

建议单独做 `Reader / Writer`，避免协议层到处手写移位。

### `MessionByteCodec.h`

```cpp
#pragma once

#include "CoreMinimal.h"

class FMessionByteWriter
{
public:
    void WriteUInt8(uint8 Value);
    void WriteUInt16LE(uint16 Value);
    void WriteUInt32LE(uint32 Value);
    void WriteUInt64LE(uint64 Value);
    void WriteFloatLE(float Value);
    void WriteUtf8String(const FString& Value);
    void WriteBytes(const TArray<uint8>& Bytes);

    const TArray<uint8>& GetData() const { return Data; }
    TArray<uint8> MoveData() { return MoveTemp(Data); }

private:
    TArray<uint8> Data;
};

class FMessionByteReader
{
public:
    explicit FMessionByteReader(const TArray<uint8>& InData);

    bool ReadUInt8(uint8& OutValue);
    bool ReadUInt16LE(uint16& OutValue);
    bool ReadUInt32LE(uint32& OutValue);
    bool ReadUInt64LE(uint64& OutValue);
    bool ReadFloatLE(float& OutValue);
    bool ReadUtf8String(FString& OutValue);
    bool ReadBytes(int32 Count, TArray<uint8>& OutBytes);

    bool IsValid() const { return bValid; }
    bool IsFullyConsumed() const { return bValid && Offset == Data.Num(); }
    int32 GetOffset() const { return Offset; }

private:
    template<typename T>
    bool ReadPodLE(T& OutValue);

private:
    const TArray<uint8>& Data;
    int32 Offset = 0;
    bool bValid = true;
};
```

### 实现要点

- `WriteUtf8String` 使用 `FTCHARToUTF8`
- 先写 `uint32 ByteLen`
- 再写 UTF-8 原始字节
- `ReadUtf8String` 要做长度越界保护

## 4. 协议层

建议把 FunctionId、CallId 和请求/响应结构编解码集中到协议层。

### `MessionGatewayProtocol.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "MessionTypes.h"

namespace MessionGatewayProtocol
{
    static constexpr uint8 MT_FunctionCall = 13;

    static constexpr uint16 Client_Login = 528;
    static constexpr uint16 Client_FindPlayer = 20722;
    static constexpr uint16 Client_QueryProfile = 3609;

    uint16 ComputeStableClientFunctionId(const FString& ApiName);

    TArray<uint8> BuildFunctionCallPacket(uint16 FunctionId, uint64 CallId, const TArray<uint8>& Payload);
    bool ParseFunctionCallPacket(const TArray<uint8>& PacketBody, FMessionFunctionCallPacket& OutPacket);

    TArray<uint8> EncodeLoginRequest(const FMessionLoginRequest& Request);
    bool DecodeLoginResponse(const TArray<uint8>& Payload, FMessionLoginResponse& OutResponse);

    TArray<uint8> EncodeFindPlayerRequest(const FMessionFindPlayerRequest& Request);
    bool DecodeFindPlayerResponse(const TArray<uint8>& Payload, FMessionFindPlayerResponse& OutResponse);
}
```

### 协议层职责边界

- 它不管 Socket
- 它不管线程
- 它只管“字节 <-> 结构”
- 所有 magic number 都集中在这里

## 5. TCP 客户端

建议把纯连接层和业务层分开。

### `MessionTcpClient.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Interfaces/IPv4/IPv4Address.h"

class FSocket;

DECLARE_DELEGATE(FMessionConnectedDelegate);
DECLARE_DELEGATE_OneParam(FMessionDisconnectedDelegate, const FString& /*Reason*/);
DECLARE_DELEGATE_OneParam(FMessionPacketReceivedDelegate, const TArray<uint8>& /*PacketBody*/);

class FMessionTcpClient
{
public:
    FMessionTcpClient();
    ~FMessionTcpClient();

    bool Connect(const FString& Host, int32 Port, FString& OutError);
    void Disconnect(const FString& Reason = TEXT("ClientDisconnect"));
    bool SendPacket(const TArray<uint8>& PacketWithLengthPrefix, FString& OutError);
    void TickReceive();

    bool IsConnected() const;

    void SetOnConnected(FMessionConnectedDelegate InDelegate);
    void SetOnDisconnected(FMessionDisconnectedDelegate InDelegate);
    void SetOnPacketReceived(FMessionPacketReceivedDelegate InDelegate);

private:
    bool ReceiveIntoBuffer(FString& OutError);
    bool TryDecodeOnePacket(TArray<uint8>& OutPacketBody);

private:
    FSocket* Socket = nullptr;
    TArray<uint8> RecvBuffer;
    FMessionConnectedDelegate OnConnected;
    FMessionDisconnectedDelegate OnDisconnected;
    FMessionPacketReceivedDelegate OnPacketReceived;
};
```

### TCP 层实现建议

- 可以先不单独开线程
- 第一版直接在 `Subsystem::Tick` 或轮询点里调用 `TickReceive()`
- 只要收包逻辑清晰即可

### `TryDecodeOnePacket` 行为

逻辑应与服务端完全对齐：

1. 如果 `RecvBuffer` 小于 4 字节，返回 false
2. 读出 `uint32 PacketLength`
3. 如果长度非法，断开连接
4. 如果当前缓冲还没收满整个包，返回 false
5. 取出 `PacketBody`
6. 从缓冲删除已消费数据

## 6. 登录子系统

建议用 `UGameInstanceSubsystem` 挂登录状态。

### `MessionLoginSubsystem.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MessionTypes.h"
#include "MessionLoginSubsystem.generated.h"

class FMessionTcpClient;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMessionLoginResultDelegate, const FMessionLoginResponse&, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMessionFindPlayerResultDelegate, const FMessionFindPlayerResponse&, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMessionConnectionStateDelegate, EMessionConnectionState, NewState);

UCLASS()
class YOURGAME_API UMessionLoginSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable)
    bool ConnectGateway(const FString& Host = TEXT("127.0.0.1"), int32 Port = 8001);

    UFUNCTION(BlueprintCallable)
    void DisconnectGateway();

    UFUNCTION(BlueprintCallable)
    bool Login(uint64 PlayerId);

    UFUNCTION(BlueprintCallable)
    bool FindPlayer(uint64 PlayerId);

    UFUNCTION(BlueprintPure)
    bool IsConnected() const;

    UFUNCTION(BlueprintPure)
    uint64 GetLoggedInPlayerId() const { return LoggedInPlayerId; }

    UFUNCTION(BlueprintPure)
    uint32 GetSessionKey() const { return SessionKey; }

public:
    UPROPERTY(BlueprintAssignable)
    FMessionLoginResultDelegate OnLoginResult;

    UPROPERTY(BlueprintAssignable)
    FMessionFindPlayerResultDelegate OnFindPlayerResult;

    UPROPERTY(BlueprintAssignable)
    FMessionConnectionStateDelegate OnConnectionStateChanged;

private:
    bool SendFunctionCall(uint16 FunctionId, uint64 CallId, const TArray<uint8>& Payload);
    uint64 AllocateCallId();

    void HandlePacketBody(const TArray<uint8>& PacketBody);
    void HandleLoginResponse(uint64 CallId, const TArray<uint8>& Payload);
    void HandleFindPlayerResponse(uint64 CallId, const TArray<uint8>& Payload);
    void HandleDisconnected(const FString& Reason);

private:
    TUniquePtr<FMessionTcpClient> TcpClient;
    EMessionConnectionState ConnectionState = EMessionConnectionState::Disconnected;
    uint64 NextCallId = 1;
    uint64 PendingLoginCallId = 0;
    uint64 PendingFindPlayerCallId = 0;
    uint64 LoggedInPlayerId = 0;
    uint32 SessionKey = 0;
};
```

## 7. 最小调用流

推荐在 `UMessionLoginSubsystem` 里按下面顺序实现：

### 7.1 Connect

1. 创建 `FMessionTcpClient`
2. 建立连接
3. 绑定收包回调
4. 记录 `ConnectionState = Connected`

### 7.2 Login

1. 生成 `CallId`
2. 编码 `FMessionLoginRequest`
3. 发送 `Client_Login`
4. 保存 `PendingLoginCallId`

### 7.3 收到 LoginResponse

1. 解析 `FMessionLoginResponse`
2. 如果成功：
   - 保存 `LoggedInPlayerId`
   - 保存 `SessionKey`
   - 自动再发一次 `FindPlayer(LoggedInPlayerId)`
3. 广播 `OnLoginResult`

### 7.4 收到 FindPlayerResponse

1. 解析 `FMessionFindPlayerResponse`
2. 广播 `OnFindPlayerResult`
3. 日志打印 `GatewayConnectionId` / `SceneId`

## 8. 日志建议

建议统一日志分类：

```cpp
DECLARE_LOG_CATEGORY_EXTERN(LogMessionGateway, Log, All);
```

至少打印这些日志：

```cpp
UE_LOG(LogMessionGateway, Log, TEXT("Connected to %s:%d"), *Host, Port);
UE_LOG(LogMessionGateway, Log, TEXT("Send Client_Login callId=%llu playerId=%llu"), CallId, PlayerId);
UE_LOG(LogMessionGateway, Log, TEXT("Login response: success=%d playerId=%llu sessionKey=%u error=%s"),
    Response.bSuccess, Response.PlayerId, Response.SessionKey, *Response.Error);
UE_LOG(LogMessionGateway, Log, TEXT("FindPlayer response: found=%d gatewayConnectionId=%llu sceneId=%u error=%s"),
    Response.bFound, Response.GatewayConnectionId, Response.SceneId, *Response.Error);
```

## 9. 第一版不要做的事情

这几项很容易把接入复杂化，建议明确禁止：

- 不要先做通用请求注册表
- 不要先做模板化 RPC 框架
- 不要先做独立工作线程池
- 不要先接 UI Widget 复杂状态机
- 不要先做自动重连
- 不要先实现 `Client_Move`、`Client_SwitchScene` 全套玩法

先把 `Login + FindPlayer` 跑通。

## 10. 第二阶段自然扩展点

等第一版跑通后，再按这个顺序扩展最自然：

1. `Client_QueryProfile`
2. `Client_QueryInventory`
3. `Client_QueryProgression`
4. `Client_QueryPawn`
5. `Client_SwitchScene`
6. `Client_Move`

## 11. 可直接复制给 UE 同学的简述

如果你不想发整篇设计稿，可以发下面这段：

```text
请在 UE 里按 5 个模块搭建 Mession 最小客户端：
1. MessionTypes：定义 Login / FindPlayer 请求响应结构
2. MessionByteCodec：实现 little-endian uint8/u16/u32/u64/float/string 编解码
3. MessionGatewayProtocol：实现 MT_FunctionCall 封包解包，支持 Client_Login=528、Client_FindPlayer=20722
4. MessionTcpClient：负责 TCP 连接、length-prefix 拆包、发送和接收
5. MessionLoginSubsystem：对外暴露 ConnectGateway / Login / FindPlayer，并在登录成功后自动发 FindPlayer

第一版只要求跑通 127.0.0.1:8001，PlayerId 可配置，日志能输出 PlayerId、SessionKey、GatewayConnectionId、SceneId。
不要先做泛化框架，先把最小闭环打通。
```
