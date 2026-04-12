# Player RPC 开发约定

本文档只描述当前 `main` 上已经落地的 Player RPC 开发方式。

## 先判断 RPC 类型

新增 RPC 前，先判断它属于哪一类。

### 1. 普通 Player 业务 RPC

适用场景：

- 查询玩家资料
- 查询背包
- 查询成长
- 查询 Pawn 状态
- 修改金币
- 修改装备
- 修改经验
- 修改血量

这类请求的共同点是：

- 请求天然带 `PlayerId`
- 最终会落到某个 `MPlayer` 子对象上执行
- 不需要在 World 层编排多个远端服务

这类 RPC 应优先走当前的 Player RPC 标准链路。

### 2. 特殊编排 RPC

适用场景：

- 登录
- 进世界
- 切场景
- 登出
- 战斗技能调用
- 任何需要跨 Login / World / Scene / Router / Mgo 多步编排的流程

这类请求不要强行塞进普通 Player RPC 清单。
它们应该继续保留在显式 workflow / service 编排代码里。

### 3. 基础设施 RPC

适用场景：

- ObjectProxy
- Router
- Login Session
- Mgo Persistence
- Gateway Downlink

这类调用不属于 Player 业务接口，不应混进 Player route list。

## 普通 Player RPC 的完整链路

当前普通 Player 业务 RPC 的结构是：

1. 客户端协议定义在 [GatewayClientMessages.h](/root/Mession/Source/Protocol/Messages/Gateway/GatewayClientMessages.h)
2. World Player 协议定义在 [WorldPlayerMessages.h](/root/Mession/Source/Protocol/Messages/World/WorldPlayerMessages.h)
3. 客户端入口显式声明在 [WorldClientServiceEndpoint.h](/root/Mession/Source/Servers/World/Services/WorldClientServiceEndpoint.h)
4. World Player 服务入口显式声明在 [WorldPlayerServiceEndpoint.h](/root/Mession/Source/Servers/World/Services/WorldPlayerServiceEndpoint.h)
5. 普通重复实现由 route list 驱动生成
6. 真正业务逻辑落在 `Players/*` 对象上的 `MFUNCTION(ServerCall)`

## 当前已经落地的普通 Player RPC

当前主干已经接通的典型普通 Player RPC 包括：

- `PlayerQueryProfile`
- `PlayerQueryPawn`
- `PlayerQueryInventory`
- `PlayerQueryProgression`
- `PlayerChangeGold`
- `PlayerEquipItem`
- `PlayerGrantExperience`
- `PlayerModifyHealth`

这说明当前这条链路已经不是模板设计，而是主干真实业务入口。

## 当前 route list 的职责

### `WorldClientPlayerRouteList.inl`

负责把客户端 `Client_*` 请求接到 World Player 服务入口。

### `WorldPlayerProxyRouteList.inl`

负责把 `Player*` 请求绑定到具体 Player 子对象节点，例如：

- `Controller`
- `Profile`
- `Inventory`
- `Progression`
- `Pawn`
- `Session`
- `Root`

当前普通 Player RPC 的核心价值，就是把“入口适配”和“对象绑定”固化下来，减少 endpoint 手写胶水。

## 新增一个普通 Player Client RPC 的步骤

### 1. 增加客户端协议

在 [GatewayClientMessages.h](/root/Mession/Source/Protocol/Messages/Gateway/GatewayClientMessages.h) 增加：

```cpp
MSTRUCT()
struct FClientXxxRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FClientXxxResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString Error;
};
```

### 2. 增加 World Player 协议

在 [WorldPlayerMessages.h](/root/Mession/Source/Protocol/Messages/World/WorldPlayerMessages.h) 增加：

```cpp
MSTRUCT()
struct FPlayerXxxRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerXxxResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;
};
```

如果 `Client` 和 `Player` 两层字段同名，客户端入口层会自动做反射拷贝。

### 3. 声明客户端入口

在 [WorldClientServiceEndpoint.h](/root/Mession/Source/Servers/World/Services/WorldClientServiceEndpoint.h) 增加：

```cpp
MFUNCTION(ClientCall, Target=World)
void Client_Xxx(FClientXxxRequest& Request, FClientXxxResponse& Response);
```

注意：

- 这里必须显式写在头文件里
- 不要尝试把 `MFUNCTION(ClientCall)` 声明藏进模板或宏生成声明里
- `MHeaderTool` 需要直接看到这些声明

### 4. 声明 World Player 服务入口

在 [WorldPlayerServiceEndpoint.h](/root/Mession/Source/Servers/World/Services/WorldPlayerServiceEndpoint.h) 增加：

```cpp
MFUNCTION(ServerCall)
MFuture<TResult<FPlayerXxxResponse, FAppError>> PlayerXxx(const FPlayerXxxRequest& Request);
```

### 5. 加入普通 Player route list

在 [WorldClientPlayerRouteList.inl](/root/Mession/Source/Servers/World/Services/WorldClientPlayerRouteList.inl) 增加一条：

```cpp
M_WORLD_CLIENT_PLAYER_ROUTE(Xxx, PlayerXxx, FClientXxxRequest, FClientXxxResponse, "player_xxx_failed")
```

在 [WorldPlayerProxyRouteList.inl](/root/Mession/Source/Servers/World/Services/WorldPlayerProxyRouteList.inl) 增加一条：

```cpp
M_WORLD_PLAYER_PROXY_ROUTE(PlayerXxx, FPlayerXxxRequest, FPlayerXxxResponse, Profile, "PlayerXxx")
```

其中第四个参数表示目标 Player 子对象。

### 6. 在 `Players/*` 上实现真正业务

例如你要把逻辑落到 `Profile`，那就在对应 Player 子对象上增加：

```cpp
MFUNCTION(ServerCall)
MFuture<TResult<FPlayerXxxResponse, FAppError>> PlayerXxx(const FPlayerXxxRequest& Request);
```

这才是实际业务实现位置。

## 当前代码生成边界

当前项目对普通 Player RPC 做的是“重复区域生成”，不是“整个 Endpoint 生成”。

### 由 route list 生成的内容

- `FPlayerProxyCallBinding` 中的普通方法
- `WorldPlayerServiceEndpoint.cpp` 中的普通 `PlayerXxx(...)`
- `FClientPlayerCallBinding` 中的普通方法
- `WorldClientServiceEndpoint.cpp` 中的普通 `Client_Xxx(...)`

### 仍然手写的内容

- `WorldClientServiceEndpoint.h` 中的 `MFUNCTION(ClientCall)` 声明
- `WorldPlayerServiceEndpoint.h` 中的 `MFUNCTION(ServerCall)` 声明
- 登录、切场景、进世界、登出等 workflow
- 战斗编排入口
- ObjectProxy / Router / Login / Mgo 基础设施调用

## 当前推荐心智模型

新增一个普通 Player 业务 RPC 时，不要先想 “endpoint 怎么写”。

推荐顺序是：

1. 业务应该落在哪个 Player 子对象
2. 这个子对象需要什么 `ServerCall`
3. `Client` / `Player` 两层协议是什么
4. 把这条路由补进两个 route list
5. 补充 `.h` 声明

也就是说：

- 业务写在 `Players/*`
- World 层只是入口适配
- 普通重复胶水交给 route list

## 不推荐的写法

- 在 `WorldClientServiceEndpoint.cpp` 手写大量普通 `Client_Xxx` 业务逻辑
- 在 `WorldPlayerServiceEndpoint.cpp` 手写大量普通 `PlayerXxx` 分发参数
- 把登录、切场景、战斗 workflow 强行塞进 route list
- 让 Gateway 承担具体业务实现

## 修改后建议验证

新增普通 Player RPC 后，至少执行：

```bash
cmake --build Build -j4
python3 Scripts/validate.py --build-dir Build --no-build
```

如果新增的是 gameplay 逻辑，建议同时补一个对应验证场景。
