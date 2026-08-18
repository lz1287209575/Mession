/**
 * @file FActorMessage.h
 * @brief Actor 消息结构 - 跨 actor 调用的统一载体.
 */
#pragma once

#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Reflect/Reflection.h"

class FAppError;

/**
 * @brief SMessageHeader - 消息头.
 *
 * 字段名是公开 ABI(codegen / wire 序列化依赖),禁止重命名。
 * 见 CodingStyle §9.1(反射字段名是公开 ABI)。
 */
struct SMessageHeader {
    uint64 SenderId    = 0;
    uint64 TargetId    = 0;
    int32  MsgType     = 0;
    uint32 PayloadSize = 0;
};

/**
 * @brief FActorMessage - actor 消息.
 *
 * - Header:发/收 actor id + 消息类型 + 负载大小
 * - Payload:序列化后的业务负载(业务自己 serialize/deserialize)
 * - ReplyPromise:仅 Call 时非空(Post 时为空)
 */
struct FActorMessage {
    SMessageHeader                                       Header;
    TByteArray                                           Payload;
    TSharedPtr<MPromise<TResult<TByteArray, FAppError>>> ReplyPromise;

    /**
     * @brief MakePost - 构造异步消息.
     */
    static FActorMessage MakePost(uint64 InSenderId, uint64 InTargetId, int32 InMsgType, TByteArray InPayload) {
        FActorMessage M;
        M.Header.SenderId    = InSenderId;
        M.Header.TargetId    = InTargetId;
        M.Header.MsgType     = InMsgType;
        M.Header.PayloadSize = static_cast<uint32>(InPayload.size());
        M.Payload            = std::move(InPayload);
        return M;
    }
};

/**
 * @brief FActorMessageWire - actor 消息的跨进程 wire 序列化结构.
 *
 * 与 FActorMessage 一一对应,但只含 wire-可序列化部分
 *     (Header 字段 + Payload)。ReplyPromise(TSharedPtr<MPromise>)不参与序列化
 *     —— 它只在进程内有效,跨进程走 MRpcChannel 标准响应通道。
 *
 * MSTRUCT() 顶层声明(MHeaderTool 扫描顶层反射类型),用于 MEchoService 的
 * OnActorMessage ServerCall 入参 / 反射产物的 BuildPayload/ParsePayload。
 *
 * 见 Docs/superpowers/specs/2026-08-13-actor-extension-design.md §4.2
 *     (跨进程 actor 调用数据流)。
 */
MSTRUCT()
struct FActorMessageWire {
    MPROPERTY()
    uint64 SenderId = 0;

    MPROPERTY()
    uint64 TargetId = 0;

    MPROPERTY()
    int32 MsgType = 0;

    MPROPERTY()
    TByteArray Payload;

    // === 阶段 C 完善:at-least-once 协议 ===
    // per-actor 单调递增 SequenceId。sender 端分配,写入 wire;
    // server 收到后回 MT_ServerPush ack 携带相同 SequenceId;
    // sender 收到 ack → 从 outbox 删 < ack.SequenceId 的所有消息。
    // 重连时 resend outbox 里残留消息,接收方按 SequenceId dedup（PoC 阶段不实现）
    MPROPERTY()
    uint64 SequenceId = 0;
};