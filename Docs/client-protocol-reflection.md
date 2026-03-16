# Client Protocol Reflection Design

## Goal

客户端协议不直接暴露内部 Server RPC。

最终分层保持为：

- Client <-> Gateway：`MessageType + PayloadStruct`
- Gateway <-> Login/World/Router：`MFUNCTION(...)` 驱动的内部 RPC

这样可以保证公网协议稳定、可版本化，同时让 Gateway 内部转发和分发继续享受反射/生成链路的收益。

## Recommended Declaration Style

客户端消息建议逐步收敛成下面这种声明风格：

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

对应返回包也保持消息协议：

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

建议主链路如下：

1. Gateway 收到客户端包，先读 `EClientMessageType`
2. 通过生成的 client manifest 找到目标函数
3. 通过生成的 payload decode helper 反序列化到请求结构体
4. 调用 `MGatewayServer::ProcessEvent(...)` 或直接生成的 invoke helper
5. 在函数体内部转成内部 RPC，例如转发到 `LoginService` / `WorldService`

这意味着：

- 外部客户端协议继续是显式 message id，不把内部函数 id 暴露到公网
- Gateway 的 `switch (EClientMessageType)` 可以逐步被生成分发表替换
- 业务层只关心请求结构体和 `MFUNCTION(...)`

## MHeaderTool Responsibilities

`MHeaderTool` 需要逐步生成四类产物：

1. client message manifest
2. `MessageType <-> Function` 映射
3. request/response 编解码 helper
4. Gateway dispatch glue

本轮已经先补了前两步的基础信息沉淀能力：

- `Build/Generated/MClientManifest.mgenerated.h`
- `Build/Generated/MReflectionManifest.mgenerated.h`

其中：

- `MClientManifest` 用来描述哪些 `MFUNCTION` 属于客户端消息入口
- `MReflectionManifest` 用来统一描述 `MCLASS/MSTRUCT/MENUM/MPROPERTY/MFUNCTION` 的归属、头文件和声明信息

## Why Not Reuse Internal RPC Directly

不建议让客户端直接发内部 RPC，原因有三点：

1. 内部 RPC 的函数名、参数布局、Endpoint 都是服务内部实现细节
2. 客户端协议需要版本演进和兼容控制，message-based 更稳
3. Gateway 需要承担安全边界，客户端入口更适合先做协议校验、鉴权和限流

## Next Implementation Steps

1. 在 `GatewayServer` 上挑一条登录消息做首个 `MFUNCTION(Client, Message=...)` 垂直切片
2. 生成 `MessageType -> decode -> invoke` glue，替换登录分支中的手写 `ParsePayload`
3. 再把 `MT_PlayerMove` 这类 World 路由消息并入同一套分发
4. 最后逐步收掉 Gateway 中手写客户端 `switch`
