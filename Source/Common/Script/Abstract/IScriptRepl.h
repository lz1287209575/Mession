/**
 * @file IScriptRepl.h
 * @brief IScriptRepl —— 脚本 REPL 的统一接口。
 *
 * @detail 前端形态未定:WebSocket(未实现)或 TCP(stdin/stdout 转发)。
 *   多 VM 并存时每个 REPL session 绑定一个 IScriptEngine 实例。Lua 侧实现见 MLuaRepl。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Abstract/EScriptLanguage.h"
#include "Common/Script/Abstract/IScriptEngine.h"

namespace mession::script {

    /**
     * @brief IScriptRepl - 交互式脚本求值会话。
     */
    class IScriptRepl {
        public:
        virtual ~IScriptRepl() = default;

        /** @brief Connect - 绑定引擎并建立会话;失败返回 false(实现可能忽略 Engine) */
        virtual bool Connect(TSharedPtr<IScriptEngine> Engine) = 0;
        /** @brief ReadLine - 同步读一行(不含换行符);流结束/失败返回 false */
        virtual bool ReadLine(MString& OutLine) = 0;
        /** @brief WriteLine - 写一行并 flush */
        virtual void WriteLine(MStringView Line) = 0;
        /** @brief Eval - 求值一段代码;成功返回结果字符串,失败返回 Err(错误文本) */
        virtual TResult<MString> Eval(MStringView Code) = 0;
        /** @brief Interrupt - 打断当前求值(实现可为 no-op) */
        virtual void Interrupt() = 0;
        /** @brief Disconnect - 释放会话资源 */
        virtual void Disconnect() = 0;

        /** @brief GetLanguage - 本 REPL 求值的语言 */
        virtual EScriptLanguage GetLanguage() const = 0;
    };

} // namespace mession::script