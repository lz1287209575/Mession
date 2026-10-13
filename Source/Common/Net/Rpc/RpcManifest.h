/**
 * @file RpcManifest.h
 * @brief RpcManifest - 按 ServerType 做服务器间 RPC 的端点解析,以及 CallRemote 模板入口。
 *
 * @detail 服务器间调用有两种寻址方式:
 *   1. 已知目标类名 → 直接 BuildRpcPayloadForRemoteCall(走 MT_RPC + FunctionId)。
 *   2. 只知 ServerType + 函数名 → 先用 FindRpcEndpoint 在反射表里反查类名,查不到再退回
 *      GetServerEndpointClassName(ServerType) 的约定类名;两者都失败则报
 *      server_call_missing_function 并记一次 unsupported 计数。
 */

#pragma once

#include "Common/Net/Rpc/RpcTransport.h"
#include "Common/Runtime/Reflect/Reflection.h"

#include <cstddef>
#include <utility>

/// 一次 RPC 端点反查的结果;两个 const char* 都指向反射系统内部的稳定存储,不需释放。
struct SRpcEndpointBinding {
    EServerType ServerType   = EServerType::Unknown;
    const char* ClassName    = nullptr; ///< 声明该函数的类名
    const char* FunctionName = nullptr; ///< 函数名
};

/**
 * @brief GetServerTypeDisplayName - 把 EServerType 映射为日志用短名。
 * @param ServerType 服务器类型
 * @return 日志名;未知类型返回 "Unknown"(不是 nullptr)
 */
const char* GetServerTypeDisplayName(EServerType ServerType);

/**
 * @brief GetServerEndpointClassName - 取某 ServerType 的约定 RPC 端点类名。
 * @param ServerType 服务器类型
 * @return 类名(Gateway→"MGatewayServer"、Echo→"MEchoService"、Player→"MPlayerService");
 *         未配置的类型返回 nullptr
 *
 * @detail 与 MEndpointCache / ServerConnection 的握手、心跳共用同一张映射,新增 ServerType
 *   时三处需同步。
 */
const char* GetServerEndpointClassName(EServerType ServerType);

/**
 * @brief FindRpcEndpoint - 在反射表中按 (ServerType, 函数名) 反查声明类。
 * @param ServerType 目标服务器类型
 * @param FunctionName 函数名
 * @param OutBinding 命中时写入(ServerType / ClassName / FunctionName)
 * @return true 命中;false FunctionName 为空或没有匹配的 MFUNCTION
 *
 * @detail 全表扫描 MObject::GetAllClasses(),复杂度与注册类数成正比;命中依据是函数的
 *   EndpointServerType 字段,不是类名。
 */
bool FindRpcEndpoint(EServerType ServerType, const char* FunctionName, SRpcEndpointBinding& OutBinding);

/**
 * @brief ReportUnsupportedRpcEndpoint - 记录一次"端点不支持"并限频告警。
 * @param ServerType   目标服务器类型
 * @param FunctionName 函数名;为空时空操作
 *
 * @detail 按 (ServerType, 函数名) 计数,只在第 1 / 10 / 100 次时打 WARN,避免热路径刷屏。
 *   计数表由 GRpcUnsupportedMutex 保护,进程生命周期内不清理。
 */
void ReportUnsupportedRpcEndpoint(EServerType ServerType, const char* FunctionName);

/**
 * @brief BuildRpcPayloadForRemoteCall - 构造服务器间 RPC 负载(优先走反射,退化到稳定 id)。
 * @param ClassName    目标类名;为空失败
 * @param FunctionName 目标函数名;为空失败
 * @param OutData      输出负载
 * @param Args         参数,按目标函数的签名顺序传入
 * @return true 构造成功;false 类名/函数名为空,或两条路径都拿不到 FunctionId
 *
 * @detail 两条路径:
 *   1. 类与函数都在反射表里 → 用 MFunction 的元数据序列化参数(可取到精确的参数类型信息);
 *   2. 反射表里查不到 → 退化到 MGET_STABLE_RPC_FUNCTION_ID(ClassName, FunctionName) 的
 *      编译期稳定 id + BuildRpcArgsPayload,要求对端能按同一个 id 找到函数。
 */
template <typename... TArgs> inline bool BuildRpcPayloadForRemoteCall(const char* ClassName, const char* FunctionName, TByteArray& OutData, TArgs&&... Args) {
    if (!ClassName || !FunctionName) {
        return false;
    }

    if (MClass* Class = MObject::FindClass(ClassName)) {
        if (MFunction* Func = Class->FindFunction(FunctionName)) {
            return BuildRpcPayloadForFunctionCall(Func, OutData, std::forward<TArgs>(Args)...);
        }
    }

    return BuildServerRpcPayload(MGET_STABLE_RPC_FUNCTION_ID(ClassName, FunctionName), BuildRpcArgsPayload(std::forward<TArgs>(Args)...), OutData);
}

/**
 * @brief MRpc - 服务器间"发了就不管"的 RPC 入口(无回调,不等响应)。
 * @note 需要请求-响应语义请用 RpcServerCall.h 的 CallServerFunction。
 */
namespace MRpc {
    /**
     * @brief CallRemote - 构造负载并以 MT_RPC 发往指定连接。
     * @param Connection   目标连接(shared_ptr 重载为空时返回 false)
     * @param ClassName    目标类名
     * @param FunctionName 目标函数名
     * @param Args         参数
     * @return true 负载构造成功且已入队;false 构造失败 / 发送缓冲满 / 连接不可用
     *
     * @detail 不注册任何待响应回调,对端的返回值会被静默丢弃 —— 适合握手、心跳这类通知型调用。
     */
    template <typename... TArgs> inline bool CallRemote(MServerConnection& Connection, const char* ClassName, const char* FunctionName, TArgs&&... Args) {
        TByteArray RpcPayload;
        if (!BuildRpcPayloadForRemoteCall(ClassName, FunctionName, RpcPayload, std::forward<TArgs>(Args)...)) {
            return false;
        }

        if (!SendServerRpcMessage(Connection, RpcPayload)) {
            return false;
        }
        return true;
    }

    /// @brief CallRemote - shared_ptr 重载;Connection 为空时返回 false,否则转发给引用版。
    template <typename... TArgs> inline bool CallRemote(const TSharedPtr<MServerConnection>& Connection, const char* ClassName, const char* FunctionName, TArgs&&... Args) {
        if (!Connection) {
            return false;
        }

        return CallRemote(*Connection, ClassName, FunctionName, std::forward<TArgs>(Args)...);
    }

    /// @brief CallRemote - INetConnection 版(Gateway 等非 MServerConnection 的出站连接走这里)。
    template <typename... TArgs> inline bool CallRemote(INetConnection& Connection, const char* ClassName, const char* FunctionName, TArgs&&... Args) {
        TByteArray RpcPayload;
        if (!BuildRpcPayloadForRemoteCall(ClassName, FunctionName, RpcPayload, std::forward<TArgs>(Args)...)) {
            return false;
        }

        if (!SendServerRpcMessage(Connection, RpcPayload)) {
            return false;
        }
        return true;
    }

    /// @brief CallRemote - INetConnection 的 shared_ptr 重载;为空时返回 false。
    template <typename... TArgs> inline bool CallRemote(const TSharedPtr<INetConnection>& Connection, const char* ClassName, const char* FunctionName, TArgs&&... Args) {
        if (!Connection) {
            return false;
        }

        return CallRemote(*Connection, ClassName, FunctionName, std::forward<TArgs>(Args)...);
    }

} // namespace MRpc
