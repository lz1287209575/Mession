/**
 * @file FSampleEchoMessages.h
 * @brief EchoService 示例协议:请求 / 响应 + 下行通知消息。
 *
 * 三组消息:
 * 1. FSampleEchoRequest / FSampleEchoResponse —— 一次来回的 ServerCall;
 * 2. FNotifyEventMsg —— 服务端 → 客户端的下行通知内容;
 * 3. FTriggerNotifyRequest —— 触发一次广播下行的 ServerCall 入口,配合
 *    MFUNCTION(CallClient) 演示端到端下行推送。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief FSampleEchoRequest - Echo 请求(待回显文本 + 目标 actor)。
 */
MSTRUCT()
struct FSampleEchoRequest {
    /// 要回显的文本
    MPROPERTY()
    MString Message;

    /// 目标 ActorId。
    ///
    /// 64-bit 位布局:[ServiceId: 高 32 位][InstId: 低 32 位]。客户端传上来的是
    /// 裸 uint64,本进程用 MServiceId::Make / Get* 拆解。
    ///
    /// Meta 标了 NonZero:值为 0 时反射校验直接失败并回报
    /// ErrorCode = "actor_id_required"(ErrorContext = "SampleEcho"),
    /// 避免这个"空 id"悄悄路由到错误的地方。
    MPROPERTY(Meta = (NonZero, ErrorCode = "actor_id_required", ErrorContext = "SampleEcho"))
    uint64 TargetActorId = 0;
};

/**
 * @brief FSampleEchoResponse - Echo 响应(回显文本 + 主叫信息)。
 */
MSTRUCT()
struct FSampleEchoResponse {
    /// 回显文本
    MPROPERTY()
    MString Echo;

    /// 主叫 ActorId,同样 64-bit 位布局。
    ///
    /// 远端拿到后可用 MServiceId::GetServiceType() 判断主叫来自哪种 Service,
    /// 再通过 MActorRouter::FindActor 寻址回调。
    MPROPERTY()
    uint64 SourceActorId = 0;

    /// 主叫所在服务器名;只用于诊断 / 日志,不参与路由
    MPROPERTY()
    MString SourceServerName;
};

/**
 * @brief FNotifyEventMsg - 服务端 → 客户端下行通知的内容。
 *
 * 客户端按 MDownlink_MEchoService_NotifyEvent 识别该下行。
 */
MSTRUCT()
struct FNotifyEventMsg {
    /// 通知文本
    MPROPERTY()
    MString Text;
};

/**
 * @brief FTriggerNotifyRequest - 触发一次广播下行的 ServerCall 入口。
 *
 * 客户端调用它,服务端随即向所有在线客户端广播一条 FNotifyEventMsg。
 */
MSTRUCT()
struct FTriggerNotifyRequest {
    /// 要广播出去的文本
    MPROPERTY()
    MString Text;
};
