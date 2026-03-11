# 🎮 Mession 分布式MMO游戏服务器框架

基于C++20的分布式游戏服务器框架，支持多服务器架构、长连接通信、属性复制等核心功能。

## 📁 项目结构

```
Mession/
├── Core/                      # 核心库
│   ├── NetCore.h            # 基础类型定义
│   └── Socket.cpp/h         # 网络Socket封装
├── Common/                   # 公共组件
│   ├── Logger.h             # 日志系统
│   └── ServerConnection.h   # 服务器长连接抽象层
├── NetDriver/               # 网络驱动
│   ├── NetObject.h         # MObject/MActor 运行时网络对象
│   ├── Replicate.h        # 属性复制系统
│   └── ReplicationDriver.h # 复制驱动
├── Servers/                 # 服务器实现
│   ├── Gateway/            # 网关服务器 (端口8001)
│   ├── Login/              # 登录服务器 (端口8002)
│   ├── World/              # 世界服务器 (端口8003)
│   └── Scene/              # 场景服务器 (端口8004)
└── CMakeLists.txt          # 构建配置
```

## 🏗️ 当前架构

```mermaid
flowchart LR
    C[Client] -->|TCP 8001 Len4 MsgType1 PayloadN| G[GatewayServer]

    G -->|后端长连接| L[LoginServer 8002]
    G -->|后端长连接| W[WorldServer 8003]
    S[SceneServer 8004] -->|后端长连接| W

    subgraph Gateway
        G1[客户端连接管理]
        G2[登录请求转发到 Login]
        G3[游戏消息转发到 World]
        G4[登录结果回包给 Client]
    end

    subgraph Login
        L1[后端握手与心跳]
        L2[SessionKey 生成]
        L3[登录结果返回 Gateway]
    end

    subgraph World
        W1[后端连接管理]
        W2[玩家进入世界]
        W3[Actor and Replication]
        W4[同步到 Scene]
    end

    subgraph Scene
        S1[主动连接 World]
        S2[场景实体管理]
        S3[进入场景 / 位置同步]
    end
```

### 模块关系图

```mermaid
flowchart TB
    Client[Client]

    subgraph GatewayServer
        GConn[客户端连接管理]
        GRoute[登录/游戏路由]
        GLoginLink[MServerConnection to Login]
        GWorldLink[MServerConnection to World]
    end

    subgraph LoginServer
        LAccept[Gateway 接入]
        LAuth[后端握手/心跳]
        LSession[会话生成与校验]
    end

    subgraph WorldServer
        WAccept[Gateway/Scene 接入]
        WPlayer[玩家与连接映射]
        WRep[MReplicationDriver]
        WSceneSync[场景同步广播]
    end

    subgraph SceneServer
        SLink[MServerConnection to World]
        SScene[场景与实体管理]
    end

    Client --> GConn
    GConn --> GRoute
    GRoute --> GLoginLink
    GRoute --> GWorldLink

    GLoginLink --> LAccept
    LAccept --> LAuth
    LAuth --> LSession

    GWorldLink --> WAccept
    WAccept --> WPlayer
    WPlayer --> WRep
    WPlayer --> WSceneSync

    SLink --> WAccept
    WSceneSync --> SScene
```

### 典型时序

```mermaid
sequenceDiagram
    participant Client
    participant Gateway
    participant Login
    participant World
    participant Scene

    Gateway->>Login: 服务器握手
    Login-->>Gateway: HandshakeAck

    Gateway->>World: 服务器握手
    World-->>Gateway: HandshakeAck

    Scene->>World: 服务器握手
    World-->>Scene: HandshakeAck

    Client->>Gateway: 登录包
    Gateway->>Login: MT_PlayerLogin
    Login-->>Gateway: MT_PlayerLogin with SessionKey
    Gateway-->>Client: LoginResponse
    Gateway->>World: MT_PlayerLogin

    World->>Scene: MT_PlayerSwitchServer
    Scene-->>Scene: 玩家进入场景

    Client->>Gateway: 移动包
    Gateway->>World: MT_PlayerDataSync
    World->>Scene: MT_PlayerDataSync
```

### 数据流图

```mermaid
flowchart LR
    CLogin[Client Login Packet] --> G1[Gateway 接收客户端包]
    G1 --> G2[转为 MT_PlayerLogin]
    G2 --> L1[Login 创建 SessionKey]
    L1 --> G3[Gateway 接收登录结果]
    G3 --> CResp[Client LoginResponse]
    G3 --> W1[World 接收玩家进入世界]
    W1 --> S1[Scene 接收玩家进入场景]

    CMove[Client Move Packet] --> G4[Gateway 转发游戏包]
    G4 --> W2[World 更新玩家状态]
    W2 --> W3[Replication and Scene Sync]
    W3 --> S2[Scene 更新实体位置]
```

### 部署视图

```mermaid
flowchart LR
    subgraph ClientSide
        C[Game Client]
    end

    subgraph ServerSide
        G[GatewayServer 8001]
        L[LoginServer 8002]
        W[WorldServer 8003]
        S[SceneServer 8004]
    end

    C -->|客户端 TCP| G
    G -->|后端长连接| L
    G -->|后端长连接| W
    S -->|后端长连接| W
```

### 网络协议视图

```mermaid
flowchart TD
    P0[统一包格式]
    P1[Length 4]
    P2[MsgType 1]
    P3[Payload N]

    P0 --> P1
    P0 --> P2
    P0 --> P3

    P2 --> CMsg[客户端消息类型 例如 Login Move]
    P2 --> SMsg[服务器消息类型 EServerMessageType]

    SMsg --> HS[MT ServerHandshake and Ack]
    SMsg --> HB[MT Heartbeat and Ack]
    SMsg --> PL[MT_PlayerLogin]
    SMsg --> PS[MT_PlayerSwitchServer]
    SMsg --> PD[MT_PlayerDataSync]
    SMsg --> PO[MT_PlayerLogout]
```

### 运行时对象视图

```mermaid
flowchart TB
    ClientConn[MClientConnection]
    TcpConn[MTcpConnection]
    LoginLink[MServerConnection]
    WorldLink[MServerConnection]
    Player[SPlayer]
    Actor[MActor]
    RepDriver[MReplicationDriver]
    SceneObj[MScene]
    SceneEntity[SSceneEntity]

    ClientConn --> TcpConn
    ClientConn --> Player

    LoginLink --> TcpConn
    WorldLink --> TcpConn

    Player --> Actor
    RepDriver --> Actor
    Player --> SceneEntity
    SceneObj --> SceneEntity
```

## 🚀 快速开始

### 编译

```bash
cd Mession
mkdir build && cd build
cmake ..
make -j4
```

### 运行

```bash
# 启动各个服务器（分开终端）
./GatewayServer   # 端口8001
./LoginServer     # 端口8002
./WorldServer    # 端口8003
./SceneServer    # 端口8004
```

## 🎯 核心功能

| 功能 | 说明 |
|------|------|
| **分布式架构** | Gateway/Login/World/Scene 多服务器架构 |
| **长连接** | TCP长连接、自动重连、心跳保活 |
| **属性复制** | UE风格的网络对象复制系统 |
| **AOI区域** | Area of Interest 区域感知系统 |
| **消息协议** | 二进制协议、粘包处理 |

## 🧩 模块职责

- **GatewayServer**: 客户端入口，维护客户端连接，并负责把登录和游戏消息分别转发到 Login / World。
- **LoginServer**: 处理登录请求、生成 `SessionKey`、返回登录结果。
- **WorldServer**: 管理玩家与世界状态，维护 `MActor`，并把玩家进入场景和位置变化同步给 Scene。
- **SceneServer**: 主动连接 World，维护场景内实体视图，处理进场和位置更新。
- **ServerConnection**: 封装服务器间长连接、握手、心跳和业务消息分发。
- **Socket / MTcpConnection**: 统一底层 TCP 包收发，处理半包、粘包和非阻塞发送。

### 关键消息职责

- **客户端登录包**: 由 `GatewayServer` 接收，转成后端 `MT_PlayerLogin` 发给 `LoginServer`。
- **`MT_PlayerLogin`**: `LoginServer` 用它生成 `SessionKey`；`GatewayServer` 再把成功结果同步给 `WorldServer`。
- **`MT_PlayerSwitchServer`**: 由 `WorldServer` 发给 `SceneServer`，表示玩家进入某个场景。
- **`MT_PlayerDataSync`**: 用于 `Gateway -> World` 的游戏数据转发，以及 `World -> Scene` 的位置/状态同步。
- **握手/心跳消息**: 全部走 `ServerConnection`，用于后端长连接认证和保活，不进入业务层消息分发。

### 三层理解

- **部署视图**: 看清楚进程和端口分别是谁对谁连。
- **网络协议视图**: 看清楚统一包格式和服务器间消息类型是怎么分层的。
- **运行时对象视图**: 看清楚连接、玩家、Actor、Scene 实体在内存中的核心关系。

## 📦 包格式

- **客户端 <-> Gateway / 服务端 MTcpConnection**: `Length(4) + MsgType(1) + Payload(N)`
- **服务器 <-> 服务器**: 同样使用 `Length(4) + MsgType(1) + Payload(N)`，其中 `MsgType` 由 `EServerMessageType` 定义
- **握手消息**: `ServerId(4) + ServerType(1) + NameLen(2) + ServerName`
- **登录结果**: `MsgType(1) + SessionKey(4) + PlayerId(8)`

## 📡 服务器间通信

```cpp
#include "Common/ServerConnection.h"

// 设置本服务器信息
MServerConnection::SetLocalInfo(1, EServerType::Gateway, "Gateway01");

// 添加远程服务器连接
auto Conn = Manager->AddServer(2, EServerType::Login, "Login01", "127.0.0.1", 8002);

// 设置回调
Conn->SetOnAuthenticated([](auto Conn, const SServerInfo& Info) {
    LOG_INFO("Server %s authenticated!", Info.ServerName.c_str());
});

// 连接
Conn->Connect();

// 发送消息
Conn->SendPlayerLogin(12345, 999999);
```

## 🔧 技术栈

- **语言**: C++20
- **构建**: CMake
- **网络**: epoll/poll, TCP
- **协议**: 自定义二进制协议

## 📝 许可证

MIT License
