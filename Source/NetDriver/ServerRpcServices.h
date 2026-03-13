#pragma once

#include "NetDriver/Reflection.h"
#include "Core/Net/NetCore.h"

// ============================================
// 服务器间 RPC 辅助工具 + Service 定义
// 使用 MReflectObject / MFunction / ERpcType
// ============================================

// 构建跨服务器 RPC 载荷：
// Data 格式统一为：
//   [FunctionId(2)][PayloadSize(4)][Payload...]
// 其中 Payload 由 MReflectArchive 负责序列化参数。
bool BuildServerRpcPayload(uint16 FunctionId, const TArray& InPayload, TArray& OutData);

// 在接收端尝试解析并调用 Service 上的 RPC 函数。
// - ServiceInstance: 具体 Service 单例（如 MWorldService 实例）
// - Data: 原始 MT_RPC 包体（不含“消息类型”字节）
// - ExpectedType: 预期的 ERpcType（如 ERpcType::ServerToServer）
// 返回：
// - true  表示已成功解析并调用对应函数
// - false 表示数据格式非法或找不到匹配的函数（调用方可选择回退到旧逻辑）
bool TryInvokeServerRpc(MReflectObject* ServiceInstance, const TArray& Data, ERpcType ExpectedType);

// ============================================
// 世界服对外 Service：Login -> World
// ============================================

class MWorldService : public MReflectObject
{
public:
    GENERATED_BODY(MWorldService, MReflectObject, 0)

public:
    // 会话校验结果回调（Login -> World）
    void Rpc_OnSessionValidateResponse(uint64 ConnectionId, uint64 PlayerId, bool bValid);
};

// 设置 World 侧会话校验回调处理器
using FWorldSessionValidateResponseHandler = TFunction<void(uint64 /*ConnectionId*/, uint64 /*PlayerId*/, bool /*bValid*/)>;

void SetWorldSessionValidateResponseHandler(const FWorldSessionValidateResponseHandler& InHandler);

// 获取 Rpc_OnSessionValidateResponse 的 FunctionId（跨进程稳定标识）
uint16 GetWorldSessionValidateResponseFunctionId();

