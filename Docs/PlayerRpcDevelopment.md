# Player RPC 开发约定

本文档只描述当前 `main` 上已经落地的 Player RPC 开发方式。

当前约定先说结论：

- 普通业务 request 只保留一份定义，直接使用 `FPlayer*Request` 或 `FWorld*Request`
- `FClient*` 主要保留给 response、notify，以及真正的客户端侧请求
- 字段级校验优先写进 `MPROPERTY(Meta=...)`
- 如果 request 只服务某个 Player 组件，可以直接定义在对应 `Player*.h`

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

1. 业务 request 定义一次，通常放在 `Source/Protocol/Messages/World/*.h`
2. 如果 request 明显归属某个 Player 组件，也可以直接放在对应组件头文件，例如 [PlayerInventory.h](/root/Mession/Source/Servers/World/Player/PlayerInventory.h) 里的 `FPlayerQueryInventoryRequest`
3. 客户端 response / notify 定义在 `Source/Protocol/Messages/Gateway/*.h`
4. 客户端入口显式声明在 [WorldClient.h](/root/Mession/Source/Servers/World/WorldClient.h)
5. World 服务入口显式声明在 [PlayerService.h](/root/Mession/Source/Servers/World/Player/PlayerService.h)
6. 普通重复适配由 [WorldClientPlayerList.inl](/root/Mession/Source/Servers/World/WorldClientPlayerList.inl) 驱动生成
7. 真正业务逻辑落在 `Player/*` 对象上的 `MFUNCTION(ServerCall)`

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

### `WorldClientPlayerList.inl`

负责把 `MWorldClient::Client_*` 入口接到 `MPlayerService::Player*`。

对象绑定不再靠第二份 route list 兜一层概念，而是直接写在 `MPlayerService` 的分发代码里，例如：

- `PlayerServiceQuery.cpp`
- `PlayerServiceModify.cpp`
- `PlayerServiceRuntime.cpp`

当前普通 Player RPC 的核心价值，就是把“客户端入口适配”固化下来，同时把对象归属保持在清晰的 service / object 边界内。

## 新增一个普通 Player Client RPC 的步骤

### 1. 先决定 request 归属

先回答两个问题：

- 这个业务最终落在哪个 `MPlayer` 子对象
- request 是通用 World 协议，还是更适合直接挂在某个组件头文件旁边

常见选择：

- 通用 World 协议：放在 `Source/Protocol/Messages/World/*.h`
- 组件自有协议：放在对应 `Player*.h`

例如背包查询 request 现在就在 [PlayerInventory.h](/root/Mession/Source/Servers/World/Player/PlayerInventory.h)。

### 2. 增加业务 request

只增加一份业务 request，例如：

```cpp
MSTRUCT()
struct FPlayerXxxRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerXxx"))
    uint64 PlayerId = 0;
};
```

规则：

- 字段级校验优先放进 `MPROPERTY(Meta=...)`
- 常用元数据包括 `NonZero`、`NonEmpty`、`Required`、`Min`、`Max`
- 只有跨字段规则或复杂业务前置条件，才额外写 `TRequestValidator<FPlayerXxxRequest>`

### 3. 增加 response

在对应 Gateway 消息头里增加客户端 response，例如：

```cpp
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

### 4. 声明客户端入口

在 [WorldClient.h](/root/Mession/Source/Servers/World/WorldClient.h) 增加：

```cpp
MFUNCTION(ClientCall, Target=World)
void Client_Xxx(FPlayerXxxRequest& Request, FClientXxxResponse& Response);
```

注意：

- 这里必须显式写在头文件里
- 不要尝试把 `MFUNCTION(ClientCall)` 声明藏进模板或宏生成声明里
- `MHeaderTool` 需要直接看到这些声明
- 普通业务 request 不再额外定义 `FClientXxxRequest`

### 5. 声明 World 服务入口

在 [PlayerService.h](/root/Mession/Source/Servers/World/Player/PlayerService.h) 增加：

```cpp
MFUNCTION(ServerCall)
MFuture<TResult<FPlayerXxxResponse, FAppError>> PlayerXxx(const FPlayerXxxRequest& Request);
```

### 6. 加入客户端 route list

在 [WorldClientPlayerList.inl](/root/Mession/Source/Servers/World/WorldClientPlayerList.inl) 增加一条：

```cpp
M_WORLD_CLIENT_PLAYER_ROUTE(Xxx, PlayerXxx, FPlayerXxxRequest, FClientXxxResponse, "player_xxx_failed")
```

### 7. 在 `MPlayerService` 写分发

`MPlayerService` 负责把 request 定位到正确对象。

例如如果它属于 `Profile`，常见写法是：

```cpp
MFuture<TResult<FPlayerXxxResponse, FAppError>> MPlayerService::PlayerXxx(
    const FPlayerXxxRequest& Request)
{
    return DispatchPlayerComponent<MPlayerProfile, FPlayerXxxResponse>(
        Request,
        &MPlayerService::FindProfile,
        &MPlayerProfile::PlayerXxx,
        "player_profile_missing",
        "PlayerXxx");
}
```

如果是多步 workflow、切场或战斗这类逻辑，就不要硬塞进这种单对象分发模板。

### 8. 在 `Player/*` 上实现真正业务

例如你要把逻辑落到 `Profile`，那就在对应 Player 子对象上增加：

```cpp
MFUNCTION(ServerCall)
MFuture<TResult<FPlayerXxxResponse, FAppError>> PlayerXxx(const FPlayerXxxRequest& Request);
```

这才是实际业务实现位置。

## 当前代码生成边界

当前项目对普通 Player RPC 做的是“客户端入口薄适配生成”，不是“整个 service 生成”。

### 由 route list 生成的内容

- `WorldClient.cpp` / `WorldClientPlayer.cpp` 中由 `WorldClientPlayerList.inl` 展开的普通 `Client_Xxx(...)`
- 对应的 request -> response 薄适配胶水

### 仍然手写的内容

- `WorldClient.h` 中的 `MFUNCTION(ClientCall)` 声明
- `PlayerService.h` 中的 `MFUNCTION(ServerCall)` 声明
- `PlayerService*.cpp` 中的对象分发逻辑
- 具体 `Player/*` 对象上的业务实现
- 登录、切场景、进世界、登出等 workflow
- 战斗编排入口
- ObjectProxy / Router / Login / Mgo 基础设施调用

## 当前推荐心智模型

新增一个普通 Player 业务 RPC 时，不要先想 “endpoint 怎么写”。

推荐顺序是：

1. 业务应该落在哪个 Player 子对象
2. 这个子对象需要什么 `ServerCall`
3. 业务 request 是否应该直接定义在该组件旁边
4. 客户端 response 长什么样
5. 把这条路由补进 `WorldClientPlayerList.inl`
6. 补充 `WorldClient.h`、`PlayerService.h` 和对象头文件声明

也就是说：

- 业务写在 `Players/*`
- World 层只做入口适配和服务分发
- 普通重复胶水交给一个 route list
- 不要再为普通业务维护一套额外的 `FClientXxxRequest`

## 不推荐的写法

- 为普通业务同时维护 `FClientXxxRequest` 和 `FPlayerXxxRequest`
- 在 `WorldClient.cpp` / `WorldClientPlayer.cpp` 手写大量普通 `Client_Xxx` 业务逻辑
- 在 `PlayerService*.cpp` 之外再额外造一层 Player route/proxy 概念
- 把登录、切场景、战斗 workflow 强行塞进 route list
- 让 Gateway 承担具体业务实现

## 修改后建议验证

新增普通 Player RPC 后，至少执行：

```bash
cmake --build Build -j4
python3 Scripts/validate.py --build-dir Build --no-build
```

如果新增的是 gameplay 逻辑，建议同时补一个对应验证场景。
