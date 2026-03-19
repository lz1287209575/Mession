#pragma once

#include "Common/MLib.h"

// 当前客户端协议消息类型
enum class EClientMessageType : uint8
{
    MT_Login = 1,           // 登录请求（客户端上行兼容壳；默认改走 MT_FunctionCall）
    MT_Handshake = 3,       // 客户端握手（客户端上行兼容壳；默认改走 MT_FunctionCall）
    MT_PlayerMove = 5,      // 玩家移动（客户端上行兼容壳；默认改走 MT_FunctionCall）
    MT_RPC = 9,             // 反射 RPC 调用（封装 MT_RPC 网络包）
    MT_Chat = 10,           // 聊天（客户端上行兼容壳；默认改走 MT_FunctionCall）
    MT_Heartbeat = 11,      // 心跳（客户端上行兼容壳；默认改走 MT_FunctionCall）
    MT_Error = 12,          // 错误
    MT_FunctionCall = 13,   // 统一函数调用入口（客户端上行默认路径）
};
