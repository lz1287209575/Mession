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
