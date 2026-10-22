/**
 * @file RpcTransport.h
 * @brief RpcTransport - 服务器间 / Client↔Gateway 报文的 wire 编解码与发送助手。
 *
 * @detail 集中三类报文的包装。所有多字节整数均走网络字节序（大端）。
 *   1. MT_RPC              负载 [FunctionId:2B][PayloadSize:4B][Payload:N]
 *   2. MT_FunctionCall     负载 [FunctionId:2B][CallId:8B][PayloadSize:4B][Payload:N]         (ServerCall 请求)
 *   3. MT_FunctionResponse 负载 [FunctionId:2B][CallId:8B][Success:1B][PayloadSize:4B][Payload:N] (ServerCall 回包)
 *   另有 Client↔Gateway 的反射 envelope(以独立 magic 区分于服务器协议):
 *      [Magic:1B][RequestId:8B][FunctionId:2B][PayloadSize:4B][Payload:N]
 *
 *   所有 Parse* 都会校验 header 长度以及 HeaderSize + PayloadSize <= Data.size(),
 *   长度不足一律返回 false(不抛异常、不越界读);调用方在 false 时应直接丢弃该包。
 */

#pragma once

#include "Common/Net/Rpc/RpcPayload.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Serialization/MessageUtils.h"

#include <cstddef>

/// Client↔Gateway envelope 的固定首字节；不得与 EServerMessageType 混用。
constexpr uint8 CLIENT_ENVELOPE_MAGIC = 0xC1;

/**
 * @brief BuildServerRpcPayload - 拼 MT_RPC 的负载 [FunctionId:2B][PayloadSize:4B][Payload:N]。
 * @param FunctionId 目标函数 id(此处不做 0 校验,空 id 由对端反射查找失败兜住)
 * @param InPayload  已序列化的参数
 * @param OutData    输出负载(先 clear)
 * @return 恒为 true
 */
bool BuildServerRpcPayload(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutData);

/**
 * @brief BuildServerRpcMessage - 在负载前补 1 字节 MT_RPC,拼成完整包。
 * @param RpcPayload BuildServerRpcPayload 产出的负载
 * @param OutPacket  输出完整包(先 clear)
 * @return 恒为 true
 *
 * @detail 供 INetConnection 版发送使用;MServerConnection 版由 SendPacket 自带包类型字节。
 */
bool BuildServerRpcMessage(const TByteArray& RpcPayload, TByteArray& OutPacket);

/**
 * @brief SendServerRpcMessage - 以 MT_RPC 发送已构造好的服务器间 RPC 负载。
 * @param Connection 目标连接(引用版;shared_ptr 版为空时返回 false)
 * @param RpcPayload BuildServerRpcPayload 产出的负载(不含包类型字节)
 * @return true 已写入发送缓冲;false 连接不可用(未连接/已关闭)或发送缓冲溢出
 *
 * @detail 只做入队,不阻塞等待对端处理;需要响应的调用由 RegisterServerCall 按 CallId
 *   完成匹配(见 RpcServerCall.h)。空负载**不会**导致失败 —— 会正常发出一个只含
 *   MT_RPC 类型字节的包。两种连接形态的差别只在拼包位置:MServerConnection 版由
 *   SendPacket 自带包类型字节,INetConnection 版先 BuildServerRpcMessage 再 Send。
 */
bool SendServerRpcMessage(MServerConnection& Connection, const TByteArray& RpcPayload);
bool SendServerRpcMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& RpcPayload);
bool SendServerRpcMessage(INetConnection& Connection, const TByteArray& RpcPayload);
bool SendServerRpcMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& RpcPayload);

// ---------------------------------------------------------------------------
// 以下 4 个是**已废弃的空壳**:旧版 Client↔Gateway 报文带 1 字节 MessageType 头
// (旧注释写作 MT_FunctionCall=13,实际枚举值也不是 13 —— EServerMessageType::MT_FunctionCall = 28)。
// Gateway 已统一改用下面的 BuildClientEnvelopePacket / ParseClientEnvelopePacket(无 MessageType)。
// 保留签名仅为了兼容仍在引用它们的外部 stub;仓库内已无实际调用点。
// 行为约定:一律清空输出参数并返回 false。
// ---------------------------------------------------------------------------
bool BuildClientFunctionPacket(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutPacket);
bool BuildClientCallPacket(uint16 FunctionId, uint64 CallId, const TByteArray& InPayload, TByteArray& OutPacket);
bool ParseClientFunctionPacket(const TByteArray& Data, uint16& OutFunctionId, uint32& OutPayloadSize, size_t& OutPayloadOffset);
bool ParseClientCallPacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, uint32& OutPayloadSize, size_t& OutPayloadOffset);

/**
 * @brief BuildClientEnvelopePacket - 拼 Client↔Gateway 反射 envelope。
 * @param FunctionId 反射函数 id;为 0 时直接失败
 * @param RequestId  客户端请求 id;下行推送固定写 0
 * @param InPayload  已序列化参数
 * @param OutPacket  输出 envelope(先 clear)
 * @return true 成功;false FunctionId == 0
 */
bool BuildClientEnvelopePacket(uint16 FunctionId, uint64 RequestId, const TByteArray& InPayload, TByteArray& OutPacket);

/**
 * @brief ParseClientEnvelopePacket - 解析 Client↔Gateway 反射 envelope 头。
 * @param Data            入站字节
 * @param OutFunctionId   反射函数 id
 * @param OutRequestId    请求 id(0 = 服务端主动推送)
 * @param OutPayloadSize  负载长度
 * @param OutPayloadOffset 负载起始偏移(= header 长度)
 * @return true 头完整且 Data 足以容纳声明的负载;false 头不足或负载被截断
 */
bool ParseClientEnvelopePacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutRequestId, uint32& OutPayloadSize, size_t& OutPayloadOffset);

/**
 * @brief BuildServerCallPacket - 拼 ServerCall 请求负载 [FunctionId:2B][CallId:8B][PayloadSize:4B][Payload:N]。
 * @param FunctionId 目标函数 id;为 0 失败
 * @param CallId     本端分配的调用 id;为 0 失败(0 被保留表示"未分配",无法用于响应匹配)
 * @param InPayload  已序列化参数
 * @param OutPayload 输出负载(先 clear)
 * @return true 成功;false FunctionId/CallId 任一为 0
 */
bool BuildServerCallPacket(uint16 FunctionId, uint64 CallId, const TByteArray& InPayload, TByteArray& OutPayload);

/**
 * @brief BuildServerCallResponsePacket - 拼 ServerCall 响应负载 [FunctionId:2B][CallId:8B][Success:1B][PayloadSize:4B][Payload:N]。
 * @param FunctionId 被调用函数 id;为 0 失败
 * @param CallId     请求带入的调用 id;为 0 失败
 * @param bSuccess   true 时 Payload 是成功的返回值,false 时是序列化的 FAppError
 * @param InPayload  已序列化的返回值或错误
 * @param OutPayload 输出负载(先 clear)
 * @return true 成功;false FunctionId/CallId 任一为 0
 */
bool BuildServerCallResponsePacket(uint16 FunctionId, uint64 CallId, bool bSuccess, const TByteArray& InPayload, TByteArray& OutPayload);

/**
 * @brief ParseServerCallPacket - 解析 ServerCall 请求包。
 * @param Data             入站字节
 * @param OutFunctionId    目标函数 id
 * @param OutCallId        调用 id
 * @param OutPayloadSize   负载长度
 * @param OutPayloadOffset 负载起始偏移(header = 2+8+4 = 14 字节)
 * @return true 头完整且负载未被截断
 */
bool ParseServerCallPacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, uint32& OutPayloadSize, size_t& OutPayloadOffset);

/**
 * @brief ParseServerCallResponsePacket - 解析 ServerCall 响应包。
 * @param Data             入站字节
 * @param OutFunctionId    被调用函数 id
 * @param OutCallId        调用 id
 * @param OutSuccess       成功标志(Success 字节 != 0)
 * @param OutPayloadSize   负载长度
 * @param OutPayloadOffset 负载起始偏移(header = 2+8+1+4 = 15 字节)
 * @return true 头完整且负载未被截断
 */
bool ParseServerCallResponsePacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, bool& OutSuccess, uint32& OutPayloadSize, size_t& OutPayloadOffset);

/**
 * @brief SendServerCallMessage - 以 MT_FunctionCall 发送 ServerCall 请求负载。
 * @param Connection    目标连接(shared_ptr 版为空返回 false)
 * @param PacketPayload BuildServerCallPacket 产出的负载
 * @return true 已入队;false 连接不可用(未连接/已关闭)/ 发送缓冲溢出
 *
 * @detail 与 SendServerRpcMessage 同理:空负载会成功发出一个只含类型字节的包。
 */
bool SendServerCallMessage(MServerConnection& Connection, const TByteArray& PacketPayload);
bool SendServerCallMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& PacketPayload);
bool SendServerCallMessage(INetConnection& Connection, const TByteArray& PacketPayload);
bool SendServerCallMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& PacketPayload);

/**
 * @brief SendServerCallResponseMessage - 以 MT_FunctionResponse 发送 ServerCall 回包负载。
 * @param Connection    回包目标连接(shared_ptr 版为空返回 false)
 * @param PacketPayload BuildServerCallResponsePacket 产出的负载
 * @return true 已入队;false 连接不可用(未连接/已关闭)/ 发送缓冲溢出
 *
 * @detail 同理,空负载不会失败。
 */
bool SendServerCallResponseMessage(MServerConnection& Connection, const TByteArray& PacketPayload);
bool SendServerCallResponseMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& PacketPayload);
bool SendServerCallResponseMessage(INetConnection& Connection, const TByteArray& PacketPayload);
bool SendServerCallResponseMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& PacketPayload);
