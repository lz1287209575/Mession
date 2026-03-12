# Mession Socket Layer Refactor Plan

## Status

Phase 1 has been implemented and validated with the current build and `scripts/validate.py`.

Implemented pieces:

- `Core/SocketPlatform.h/.cpp`
- `Core/SocketAddress.h`
- `Core/SocketHandle.h`
- `Core/PacketCodec.h`
- `MTcpConnection` active and accepted socket paths
- `MServerConnection` transport unification over `MTcpConnection`
- listener socket RAII with `MSocketHandle`
- accepted socket value object `SAcceptedSocket`
- thin poll helper `MSocketPoller`

Current result:

- packet framing is shared
- active and passive TCP paths share the same transport type
- listener and accepted socket ownership are cleaner
- repeated poll setup has been reduced in the main server loops

Remaining follow-up work is now mostly cleanup and possible phase 2 refinement, not core transport unification.

## Goal

Refactor the current socket/network layer into a small cross-platform stack that is easier to maintain without rewriting the server architecture in one pass.

This first phase focuses on:

- separating platform-specific socket operations from higher-level connection logic
- making `MTcpConnection` the single TCP transport implementation
- extracting length-prefixed packet framing into a shared codec
- reducing duplicated send/recv/frame parsing logic in `MServerConnection`

This phase does **not** attempt to introduce a full async framework, a full unified reactor, or Linux-only performance backends such as `epoll`.

## Current Problems

### 1. Transport and protocol responsibilities are mixed

`Core/Socket` currently contains:

- platform socket setup
- raw send/recv
- connection lifetime
- recv/send buffers
- length-prefixed packet parsing

At the same time, `Common/ServerConnection` also owns:

- raw socket creation
- non-blocking connect
- raw send/recv
- its own recv buffer
- its own packet splitting

This duplicates transport logic in two places.

### 2. Platform details leaked too high

Before this refactor, higher layers still directly depended on:

- `TSocketFd`
- `pollfd`
- `poll()`
- manual `connect()`, `send()`, `recv()`

This makes the code harder to evolve toward other backends later.

### 3. Packet framing is duplicated

The `Length(4) + Payload(N)` framing logic exists in both:

- `MTcpConnection`
- `MServerConnection`

That makes bug fixes and protocol changes risky.

### 4. Active and passive TCP paths are split

Accepted client/server sockets use `MTcpConnection`, but active backend connections in `MServerConnection` bypass it and manage sockets directly.

That meant the project did not actually have one canonical TCP connection implementation.

## Refactor Principles

- keep the first phase small and incremental
- preserve current packet format and server behavior
- avoid touching business logic unless required by the transport boundary
- make the transport layer reusable by both accepted and actively connected sockets
- prefer composition over inheritance where possible

## Target Layering

The target structure for phase 1 is:

1. `SocketPlatform`: OS-specific socket API wrapper
2. `SocketHandle` and `SocketAddress`: basic socket value types
3. `PacketCodec`: length-prefixed framing
4. `MTcpConnection`: single TCP transport implementation
5. `MServerConnection`: server protocol wrapper built on top of `MTcpConnection`

## Layer Design

### 1. `MSocketPlatform`

New files:

- `Core/SocketPlatform.h`
- `Core/SocketPlatform.cpp`

Responsibilities:

- initialize socket subsystem
- create sockets
- close sockets
- set common socket options
- wrap platform-specific error handling
- wrap raw `send` and `recv`

This layer must not know anything about:

- players
- message types
- packet framing
- reconnect logic

Suggested interface:

```cpp
class MSocketPlatform
{
public:
    static bool EnsureInit();

    static TSocketFd CreateTcpSocket();
    static void CloseSocket(TSocketFd SocketFd);

    static bool SetNonBlocking(TSocketFd SocketFd, bool bNonBlocking);
    static bool SetNoDelay(TSocketFd SocketFd, bool bNoDelay);
    static bool SetReuseAddress(TSocketFd SocketFd, bool bReuseAddress);

    static int GetLastError();
    static bool IsWouldBlock(int Error);
    static bool IsConnectInProgress(int Error);

    static int32 Send(TSocketFd SocketFd, const void* Data, uint32 Size);
    static int32 Recv(TSocketFd SocketFd, void* Buffer, uint32 Size);
};
```

### 2. `SSocketAddress`

New file:

- `Core/SocketAddress.h`

Responsibilities:

- represent IP and port in a transport-friendly form
- provide helpers to validate or build socket addresses

Suggested interface:

```cpp
struct SSocketAddress
{
    FString Ip;
    uint16 Port = 0;

    bool IsValid() const;
};
```

This can stay IPv4-only in phase 1 to match the current code.

### 3. `MSocketHandle`

New file:

- `Core/SocketHandle.h`

Responsibilities:

- RAII ownership of a socket descriptor
- prevent raw fd lifetime from leaking through the codebase

Suggested interface:

```cpp
class MSocketHandle
{
public:
    MSocketHandle() = default;
    explicit MSocketHandle(TSocketFd InSocketFd);
    ~MSocketHandle();

    MSocketHandle(MSocketHandle&& Other) noexcept;
    MSocketHandle& operator=(MSocketHandle&& Other) noexcept;

    bool IsValid() const;
    TSocketFd Get() const;
    TSocketFd Release();
    void Reset(TSocketFd NewSocketFd = INVALID_SOCKET_FD);

private:
    TSocketFd SocketFd = INVALID_SOCKET_FD;
};
```

### 4. `MLengthPrefixedPacketCodec`

New file:

- `Core/PacketCodec.h`

Responsibilities:

- encode a payload into `Length(4) + Payload(N)`
- decode one full packet from a recv buffer
- keep packet framing separate from socket I/O

Suggested interface:

```cpp
class MLengthPrefixedPacketCodec
{
public:
    static bool EncodePacket(const TArray& Payload, TArray& OutBytes);
    static bool TryDecodePacket(TArray& InOutRecvBuffer, TArray& OutPayload);
};
```

Implementation notes:

- use `memcpy` instead of pointer casts
- validate `PacketSize > 0 && PacketSize <= MAX_PACKET_SIZE`
- leave network byte order decisions unchanged for now

### 5. `MTcpConnection`

Existing files to refactor:

- `Core/Socket.h`
- `Core/Socket.cpp`

Responsibilities after refactor:

- own the socket handle
- manage recv/send buffers
- expose stream I/O plus complete packet extraction
- work for both accepted sockets and actively connected sockets

Suggested phase 1 interface:

```cpp
class MTcpConnection : public INetConnection
{
public:
    explicit MTcpConnection(TSocketFd InSocketFd);

    static TSharedPtr<MTcpConnection> ConnectTo(const SSocketAddress& Address, float TimeoutSeconds);

    bool Send(const void* Data, uint32 Size) override;
    bool Receive(void* Buffer, uint32 Size, uint32& BytesRead) override;
    bool ReceivePacket(TArray& OutPacket) override;
    bool ProcessRecvBuffer(TArray& OutPacket) override;
    bool FlushSendBuffer() override;

    void SetNonBlocking(bool bNonBlocking) override;
    bool IsConnected() const override;
    void Close() override;

    const FString& GetRemoteAddress() const;
    uint16 GetRemotePort() const;

private:
    bool PumpRecvBytes();
};
```

Phase 1 landing notes:

- `Send()` should continue to accept raw payload and wrap it using the shared packet codec
- accepted sockets may be constructed either from raw `TSocketFd` or from `MSocketHandle`
- active backend connections should move to `ConnectTo(...)`

### 6. `MServerConnection`

Existing files to refactor:

- `Common/ServerConnection.h`
- `Common/ServerConnection.cpp`

Responsibilities after refactor:

- keep server protocol state
- keep reconnect and heartbeat logic
- keep handshake and application callbacks
- delegate all raw transport I/O to `MTcpConnection`

Proposed internal change:

```cpp
class MServerConnection : public TEnableSharedFromThis<MServerConnection>
{
private:
    TSharedPtr<MTcpConnection> Transport;
    EConnectionState State = EConnectionState::Disconnected;
    SServerConnectionConfig Config;

    float HeartbeatTimer = 0.0f;
    float ReconnectTimer = 0.0f;

    TFunction<void(TSharedPtr<MServerConnection>)> OnConnectCallback;
    TFunction<void(TSharedPtr<MServerConnection>)> OnDisconnectCallback;
    TFunction<void(TSharedPtr<MServerConnection>, uint8, const TArray&)> OnMessageCallback;
    TFunction<void(TSharedPtr<MServerConnection>, const SServerInfo&)> OnServerAuthenticatedCallback;
};
```

What should be removed from `MServerConnection` in phase 1:

- direct `SocketFd` ownership
- direct raw `connect()`
- direct raw `send()` and `recv()`
- local packet framing logic
- local recv buffer management

What should remain:

- `Connect()`
- `Disconnect()`
- `Tick()`
- `Send(uint8 Type, ...)`
- handshake/heartbeat handling
- reconnect timer and connection state

## What Stays Unchanged In Phase 1

To keep scope under control, phase 1 should **not** change:

- the server tick architecture
- the per-server `poll()` loops
- the current packet format
- current gameplay message structures
- current router/login/world/scene service responsibilities

This means `Gateway`, `Login`, `World`, `Router`, `Scene`, and `GameServer` can continue to:

- own their listen socket
- call `MSocket::CreateListenSocket()`
- call `MSocket::Accept()`
- build `pollfd` arrays
- process readable connections in their existing loops

## Phase 1 Migration Order

### Step 1: Add `MSocketPlatform`

Move platform-specific code out of `Core/Socket.cpp` into `SocketPlatform`.

Expected result:

- `Socket.cpp` gets smaller
- WinSock/POSIX branches are isolated

### Step 2: Add `MLengthPrefixedPacketCodec`

Move shared framing logic out of both `MTcpConnection` and `MServerConnection`.

Expected result:

- only one implementation of `Length(4) + Payload(N)` remains

### Step 3: Refactor `MTcpConnection`

Make `MTcpConnection` the canonical transport type for both accepted and actively connected sockets.

Expected result:

- one transport implementation for all TCP paths

### Step 4: Refactor `MServerConnection`

Replace raw socket logic with composition over `MTcpConnection`.

Expected result:

- `MServerConnection` becomes a protocol/session wrapper, not a transport implementation

### Step 5: Keep server poll loops mostly as-is

Do not introduce a heavy central event loop yet. A thin helper is acceptable if it only removes duplicated `pollfd` setup.

Expected result:

- low-risk rollout
- easier debugging of regressions

## Future Phase 2

Once phase 1 is stable, the next optional step is a dedicated poller or event loop abstraction:

- `MSocketPoller`
- or `MNetEventLoop`

That phase can later grow into:

- Linux `epoll`
- macOS `kqueue`
- Windows IOCP or continued `WSAPoll`

This should happen only after transport and framing are already unified.

## Risks

### 1. Silent protocol regressions

Because packet framing currently exists in more than one place, moving it into a codec must be verified with:

- current login validation flow
- current reconnect validation flow
- backend handshake and heartbeat

### 2. Mixed ownership during migration

If some code paths still use raw sockets while others use `MTcpConnection`, the code can temporarily become harder to reason about.

Mitigation:

- migrate `MServerConnection` completely in one focused step
- move listener sockets to `MSocketHandle` once transport changes are stable

### 3. Scope creep

It will be tempting to also solve:

- central polling
- IPv6
- better error/result types
- message codec redesign
- platform-specific performance optimizations

These should be deferred until phase 1 is landed.

## Acceptance Criteria For Phase 1

Phase 1 is complete when:

- `MServerConnection` no longer directly owns or manipulates raw sockets
- length-prefixed packet framing exists in one shared codec only
- accepted and active TCP connections both use `MTcpConnection`
- listener sockets use RAII ownership
- existing build succeeds on current targets
- `scripts/validate.py` still passes

## Recommended Implementation Scope

Start with these files:

- `Core/SocketPlatform.h`
- `Core/SocketPlatform.cpp`
- `Core/PacketCodec.h`
- `Core/Socket.h`
- `Core/Socket.cpp`
- `Common/ServerConnection.h`
- `Common/ServerConnection.cpp`

Avoid touching gameplay/server files until the transport refactor forces a call-site update.

## Phase 1 Outcome

The original phase 1 goal is now met.

Recommended next steps:

1. Keep current abstractions stable and avoid adding another large transport rewrite.
2. Decide whether `MSocketPoller` should remain a thin helper or evolve into a more explicit event-loop abstraction.
3. Shift attention back to gameplay-facing gaps: state cleanup, distributed replication, and tests.
