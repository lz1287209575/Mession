#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Script/Abstract/IScriptEngine.h"
#include "Common/Script/Abstract/IScriptModule.h"
#include "Common/Script/Abstract/IScriptRepl.h"
#include "Common/Script/Abstract/ScriptErrorCodes.h"

namespace mession::script {

    // ScriptBridge 工具 — 占位,后续 Script 桥接层 plan 充实具体实现
    class MScriptBridge {
        public:
        // 把 VM 抛出的原生异常包装成 FScriptError,通过 GetErrorString 抽堆栈
        static FScriptError WrapVmException(IScriptEngine* Engine, int32 VmState, void* VmFrame);

        // 通过反射把 MFunction 的参数序列化为 Script 调用参数
        static TResult<SScriptArgs> BuildScriptArgs(MFunctionObject* Fn, const TVariant* Params, size_t ParamCount);
    };

} // namespace mession::script