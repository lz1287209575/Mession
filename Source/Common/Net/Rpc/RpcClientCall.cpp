// step-2: 原 RpcClientCall.cpp 几乎所有函数已删除。
//
//   原 deferred call 注册表 / ClientDownlink 反查 / BuildClientFunctionArgsPayload
//   全部依赖 IClientResponseTarget / MClientDownlink / MessageType dispatch,
//   这些符号都已被 step-2 拆。新 Client↔Gateway envelope 由
//   RpcTransport.cpp::BuildClientEnvelopePacket / ParseClientEnvelopePacket
//   负责。
//
// 保留这个空 cpp 文件占位,以便 mession_common 静态库二进制稳定。
// 后续 task 引入 MFUNCTION(Async, CallClient) 时,在本文件加真正实现。
