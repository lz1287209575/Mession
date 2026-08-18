#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Abstract/EScriptLanguage.h"
#include "Common/Script/Abstract/IScriptEngine.h"

namespace mession::script {

    // IScriptRepl 统一 REPL 接口
    // 前端:WebSocket(未实现)或 TCP(stdin/stdout 转发)
    // 多 VM 并存:每个 REPL session 绑定一个 IScriptEngine 实例
    class IScriptRepl {
        public:
        virtual ~IScriptRepl() = default;

        virtual bool             Connect(TSharedPtr<IScriptEngine> Engine) = 0;
        virtual bool             ReadLine(MString& OutLine)                = 0;
        virtual void             WriteLine(MStringView Line)               = 0;
        virtual TResult<MString> Eval(MStringView Code)                    = 0;
        virtual void             Interrupt()                               = 0;
        virtual void             Disconnect()                              = 0;

        virtual EScriptLanguage GetLanguage() const = 0;
    };

} // namespace mession::script