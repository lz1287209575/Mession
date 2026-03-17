# Client Protocol Reflection Design

## Goal

客户端协议的长期方向是统一函数声明驱动，而不是长期维护独立的客户端消息号体系。

如果目的是直接交给 UE 侧或 Agent 快速打通当前 Gateway，请优先看：

- [ue-gateway-quickstart.md](/workspaces/Mession/Docs/ue-gateway-quickstart.md)
- [client-unified-function-call.md](/workspaces/Mession/Docs/client-unified-function-call.md)

最终分层保持为：

- 业务层：`FunctionName + Payload + RoutePolicy`
- 传输层：自动派生的 `FunctionID` 或兼容包头
- Gateway / Login / World / Router：统一由 `MFUNCTION(...)` 驱动

短期兼容期内，`Client <-> Gateway` 仍可保留 `MessageType + PayloadStruct`；
但长期目标已经切换为统一函数调用。

## Recommended Declaration Style

客户端可调用入口建议逐步收敛成下面这种声明风格：

```cpp
MSTRUCT()
struct FC2S_LoginRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MCLASS()
class MGatewayServer : public MNetServerBase, public MReflectObject
{
public:
    MGENERATED_BODY(MGatewayServer, MReflectObject, 0)

    MFUNCTION(Client, Message=MT_Login, Reliable=true)
    void Client_Login(const FC2S_LoginRequest& Request);
};
```

对应返回结构也应逐步收敛成函数驱动，而不是继续扩展独立消息号：

```cpp
MSTRUCT()
struct FS2C_LoginResponse
{
    MPROPERTY()
    uint32 SessionKey = 0;

    MPROPERTY()
    uint64 PlayerId = 0;
};
```

## Runtime Flow

建议长期主链路如下：

1. Client 发起 `ClassName + FunctionName + Payload`
2. 运行时自动派生 `FunctionID`
3. Gateway 通过生成的 client-call manifest 找到目标函数
4. 通过生成的 payload decode helper 反序列化到请求结构体
5. 按函数声明上的 Route / Auth / Wrap / Target 执行本地消费或转发

这意味着：

- 业务层只关心函数和 payload
- `FunctionID` 只是统一规则自动派生的传输细节
- Gateway 的客户端入口分发可以逐步从 `MessageType` 过渡到 `FunctionID`
- Client / Gateway / OtherSvr 的接口认知逐步统一

## MHeaderTool Responsibilities

`MHeaderTool` 需要逐步生成以下产物：

1. client-callable manifest
2. `FunctionID <-> Function` 映射
3. request/response 编解码 helper
4. Gateway dispatch glue
5. UE / Agent 可消费的导出 helper 或 manifest

当前已经有基础信息沉淀能力：

- `Build/Generated/MClientManifest.mgenerated.h`
- `Build/Generated/MReflectionManifest.mgenerated.h`

其中：

- `MClientManifest` 用来描述哪些 `MFUNCTION` 属于客户端入口
- `MReflectionManifest` 用来统一描述 `MCLASS/MSTRUCT/MENUM/MPROPERTY/MFUNCTION` 的归属、头文件和声明信息

## Why Not Reuse Internal RPC Directly

统一函数调用不等于让客户端直接发内部所有内部 RPC，原因有三点：

1. 内部 RPC 的函数名、参数布局、Endpoint 都是服务内部实现细节
2. Gateway 仍然需要承担安全边界、鉴权和限流
3. 只有显式声明为客户端可调用的函数，才应暴露到客户端入口 manifest

## Next Implementation Steps

1. 定义客户端统一函数调用的兼容 wire format
2. 生成 `FunctionID -> decode -> invoke` glue
3. 先挑 `Handshake / Login / Chat` 做首批垂直切片
4. 脚本同时验证旧 `MessageType` 路径与新函数调用路径
5. 最后逐步收掉按业务消息号扩展客户端入口的方式
