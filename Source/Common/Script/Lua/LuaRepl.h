/**
 * @file LuaRepl.h
 * @brief MLuaRepl —— 基于 stdin/stdout 的 Lua REPL 实现。
 *
 * @detail 每个实例自带一个独立的 MLuaScriptState,与 MLuaEngine 的 VM 互不干扰;
 *   求值结果目前不回显(见 Eval)。ReadLine / WriteLine 直接走 C stdio,是阻塞式实现。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Abstract/EScriptLanguage.h"
#include "Common/Script/Abstract/IScriptEngine.h"
#include "Common/Script/Abstract/IScriptRepl.h"
#include "Common/Script/Lua/LuaScriptState.h"

namespace mession::script::lua {

    /**
     * @brief MLuaRepl - 本地终端 REPL(stdin 读行、stdout 写行)。
     */
    class MLuaRepl : public mession::script::IScriptRepl {
        public:
        MLuaRepl();
        ~MLuaRepl() override;

        /**
         * @brief Connect - 新建一个独立 VM(忽略传入的 Engine)。
         * @return VM 创建成功时 true
         */
        bool Connect(TSharedPtr<mession::script::IScriptEngine> Engine) override;
        bool ReadLine(MString& OutLine) override;
        void WriteLine(MStringView Line) override;
        /**
         * @brief Eval - 执行一段源码。
         * @return 成功固定返回 Ok("ok")(不回显表达式的值);失败返回 Err(VM 错误文本)
         *
         * @detail 未 Connect 时 Err("repl not connected")。
         */
        TResult<MString>                 Eval(MStringView Code) override;
        void                             Interrupt() override;
        void                             Disconnect() override;
        mession::script::EScriptLanguage GetLanguage() const override {
            return mession::script::EScriptLanguage::Lua;
        }

        private:
        TUniquePtr<MLuaScriptState> State;
    };

} // namespace mession::script::lua