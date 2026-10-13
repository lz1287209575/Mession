/**
 * @file LuaRepl.cpp
 * @brief 实现基于 stdin/stdout 的 Lua REPL:读行、写行、执行、断开。
 */
#include "Common/Script/Lua/LuaRepl.h"
#include "Common/Script/Abstract/IScriptRepl.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace mession::script::lua {

    MLuaRepl::MLuaRepl()  = default;
    MLuaRepl::~MLuaRepl() = default;

    bool MLuaRepl::Connect(TSharedPtr<mession::script::IScriptEngine> /*Engine*/) {
        State = std::make_unique<MLuaScriptState>();
        return State->IsValid();
    }

    /**
     * @brief ReadLine - 从 stdin 读一行(阻塞)。
     * @param OutLine 去掉了行尾 \n / \r 的内容
     * @return 读到内容返回 true;EOF 或读失败返回 false
     *
     * @detail 单行上限 4095 字节,超出部分留在后续行的开头。
     */
    bool MLuaRepl::ReadLine(MString& OutLine) {
        char Buf[4096];
        if (!std::fgets(Buf, sizeof(Buf), stdin)) {
            return false;
        }
        size_t Len = std::strlen(Buf);
        while (Len > 0 && (Buf[Len - 1] == '\n' || Buf[Len - 1] == '\r')) {
            --Len;
        }
        OutLine.assign(Buf, Len);
        return true;
    }

    /**
     * @brief WriteLine - 把 Line 写到 stdout 并换行 + flush。
     */
    void MLuaRepl::WriteLine(MStringView Line) {
        MString Msg(Line.data(), Line.size());
        std::fwrite(Msg.c_str(), 1, Msg.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

    TResult<MString> MLuaRepl::Eval(MStringView Code) {
        if (!State || !State->IsValid()) {
            return TResult<MString>::Err(MString("repl not connected"));
        }
        MString Src(Code.data(), Code.size());
        MString Result = State->LoadBuffer("repl", Src.c_str(), Src.size());
        if (!Result.empty()) {
            return TResult<MString>::Err(Result);
        }
        return TResult<MString>::Ok(MString("ok"));
    }

    /** @brief Interrupt - 空实现(无法打断正在执行的 chunk) */
    void MLuaRepl::Interrupt() {
    }
    /** @brief Disconnect - 销毁 REPL 自己的 VM;之后 Eval 会返回 "repl not connected" */
    void MLuaRepl::Disconnect() {
        State.reset();
    }

} // namespace mession::script::lua