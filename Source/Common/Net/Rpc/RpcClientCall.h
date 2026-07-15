#pragma once

// step-2: 原 RpcClientCall 整个文件基本被清空。
//
//   原 SClientCallContext / GetCurrentClientConnectionId / CaptureCurrentClientCallContext
//   / RegisterDeferredClientCall / BuildClientFunctionArgsPayload / BuildClientFunctionCallPacket*
//   / FindClientDownlinkFunction* 全部删除 —— 它们依赖的 IClientResponseTarget /
//   MClientDownlink / MessageType dispatch 整条路径已拆。
//
//   新 Client↔Gateway envelope 由 RpcTransport.cpp::BuildClientEnvelopePacket /
//   ParseClientEnvelopePacket 负责,纯反射(只看 FunctionId + Payload)。
//   业务侧如果需要给 UE 推下行,走 MHeaderTool 生成的 MClientDownlinkManifest +
//   静态 stub(后续 task 引入)。

#include <cstdint>

namespace RpcClientCall
{
    // 整文件目前为占位过渡。后续 task 引入 MFUNCTION(Async, CallClient) 时,
    // 这里会增加:
    //   - bool SendClientDownlink(TSharedPtr<INetConnection>, uint16 FunctionId, const TByteArray& Payload);
    //   - uint16 ResolveClientDownlinkFunctionId(const char* OwnerType, const char* FunctionName);
    // 当前为空,但仍保留头文件符号以便 binary 链接稳定。
}