/**
 * @file ScriptBridge.h
 * @brief MScriptBridge —— 脚本桥接工具(仅声明,无实现)。
 *
 * @detail NOTE: 本文件只有声明,仓库内没有对应 .cpp,也没有任何调用点;
 *   属早期规划遗留。后续 Script 桥接层落地时再补实现,当前链接这两个静态方法会失败。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Script/Abstract/IScriptEngine.h"
#include "Common/Script/Abstract/IScriptModule.h"
#include "Common/Script/Abstract/IScriptRepl.h"
#include "Common/Script/Abstract/ScriptErrorCodes.h"

namespace mession::script {

    /**
     * @brief MScriptBridge - 脚本桥接工具(占位,尚未实现)。
     */
    class MScriptBridge {
        public:
        /**
         * @brief WrapVmException - 把 VM 原生异常包装成 FScriptError。
         * @param Engine    出错脚本引擎
         * @param VmState   VM 的错误状态码(如 lua_State 的返回码)
         * @param VmFrame   VM 栈帧/错误值,供实现抽取堆栈
         * @return 带 Code / Message / StackTrace 的错误描述
         */
        static FScriptError WrapVmException(IScriptEngine* Engine, int32 VmState, void* VmFrame);

        /**
         * @brief BuildScriptArgs - 反射地把 C++ 参数序列化为脚本调用参数。
         * @param Fn         被调用的反射函数(提供形参类型描述)
         * @param Params     实参数组
         * @param ParamCount Params 元素个数
         * @return 序列化后的 SScriptArgs;失败返回 Err
         */
        static TResult<SScriptArgs> BuildScriptArgs(MFunctionObject* Fn, const TVariant* Params, size_t ParamCount);
    };

} // namespace mession::script