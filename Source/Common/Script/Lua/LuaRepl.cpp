#include "Common/Script/Lua/LuaRepl.h"
#include "Common/Script/Abstract/IScriptRepl.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace mession::script::lua {

MLuaRepl::MLuaRepl() = default;
MLuaRepl::~MLuaRepl() = default;

bool MLuaRepl::Connect(TSharedPtr<mession::script::IScriptEngine> /*Engine*/)
{
    State = std::make_unique<MLuaScriptState>();
    return State->IsValid();
}

bool MLuaRepl::ReadLine(MString& OutLine)
{
    char Buf[4096];
    if (!std::fgets(Buf, sizeof(Buf), stdin))
    {
        return false;
    }
    size_t Len = std::strlen(Buf);
    while (Len > 0 && (Buf[Len - 1] == '\n' || Buf[Len - 1] == '\r'))
    {
        --Len;
    }
    OutLine.assign(Buf, Len);
    return true;
}

void MLuaRepl::WriteLine(MStringView Line)
{
    MString Msg(Line.data(), Line.size());
    std::fwrite(Msg.c_str(), 1, Msg.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

TResult<MString> MLuaRepl::Eval(MStringView Code)
{
    if (!State || !State->IsValid())
    {
        return TResult<MString>::Err(MString("repl not connected"));
    }
    MString Src(Code.data(), Code.size());
    MString Result = State->LoadBuffer("repl", Src.c_str(), Src.size());
    if (!Result.empty())
    {
        return TResult<MString>::Err(Result);
    }
    return TResult<MString>::Ok(MString("ok"));
}

void MLuaRepl::Interrupt() {}
void MLuaRepl::Disconnect() { State.reset(); }

} // namespace mession::script::lua