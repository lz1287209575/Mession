#include "Common/Script/Lua/MLuaFormat.h"
#include "Common/Runtime/StringUtils.h"

#include <fmt/format.h>
#include <fmt/args.h>

namespace mession::script::lua {

namespace {

// 收集 Lua 栈 args 到 fmt::dynamic_format_arg_store(运行时类型擦除)
struct FArgCollector {
    fmt::dynamic_format_arg_store<fmt::format_context> Store;

    void Collect(lua_State* L, int First, int Last)
    {
        for (int i = First; i <= Last; ++i) {
            int Type = lua_type(L, i);
            switch (Type) {
            case LUA_TNUMBER: {
                if (lua_isinteger(L, i)) {
                    Store.push_back(static_cast<int64>(lua_tointeger(L, i)));
                } else {
                    Store.push_back(lua_tonumber(L, i));
                }
                break;
            }
            case LUA_TBOOLEAN:
                Store.push_back(lua_toboolean(L, i) != 0);
                break;
            case LUA_TSTRING: {
                size_t      Len = 0;
                const char* P   = lua_tolstring(L, i, &Len);
                Store.push_back(MString(P, Len));
                break;
            }
            default:
                // 不支持的类型 → nil 占位(fmt 会输出 "<nil>")
                Store.push_back(MString("<nil>"));
                break;
            }
        }
    }
};

int FmtFmt(lua_State* L)
{
    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "fmt: template must be string");
    }
    size_t      FmtLen = 0;
    const char* FmtPtr = lua_tolstring(L, 1, &FmtLen);
    MStringView FmtStr(FmtPtr, FmtLen);

    FArgCollector C;
    int Top = lua_gettop(L);
    if (Top >= 2) {
        C.Collect(L, 2, Top);
    }
    try {
        MString Out = fmt::vformat(FmtStr, C.Store);
        lua_pushlstring(L, Out.c_str(), Out.size());
        return 1;
    } catch (const std::exception& Ex) {
        return luaL_error(L, "fmt_error: %s", Ex.what());
    }
}

int FmtConcat(lua_State* L)
{
    int Top = lua_gettop(L);
    if (Top < 1) {
        lua_pushlstring(L, "", 0);
        return 1;
    }
    MStringBuilder B;
    for (int i = 1; i <= Top; ++i) {
        size_t      Len = 0;
        const char* P   = nullptr;
        if (lua_isstring(L, i)) {
            P = lua_tolstring(L, i, &Len);
        } else {
            // 非 string:走 tostring()
            lua_getglobal(L, "tostring");
            lua_pushvalue(L, i);
            if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
                return luaL_error(L, "concat: tostring failed");
            }
            P = lua_tolstring(L, -1, &Len);
        }
        MStringBuilder::Append(B, MString(P, Len));
        if (i > 1 && lua_isstring(L, i)) {
            // 之前用过的临时 tostring 结果弹出
        }
    }
    // 清理可能遗留的 tostring 临时值
    while (lua_gettop(L) > Top) {
        lua_pop(L, 1);
    }
    MString Out = MStringBuilder::ToString(B);
    lua_pushlstring(L, Out.c_str(), Out.size());
    return 1;
}

int FmtSplit(lua_State* L)
{
    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "split: str must be string");
    }
    if (!lua_isstring(L, 2) || lua_rawlen(L, 2) != 1) {
        return luaL_error(L, "split: sep must be single char string");
    }
    size_t      SLen = 0;
    const char* S    = lua_tolstring(L, 1, &SLen);
    const char* Sep  = lua_tolstring(L, 2, nullptr);
    MString Src(S, SLen);
    char Delim = Sep[0];
    TVector<MString> Parts = MStringUtil::Split(Src, Delim);

    lua_newtable(L);
    for (size_t i = 0; i < Parts.size(); ++i) {
        lua_pushlstring(L, Parts[i].c_str(), Parts[i].size());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

int FmtTrim(lua_State* L)
{
    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "trim: str must be string");
    }
    size_t      Len = 0;
    const char* P   = lua_tolstring(L, 1, &Len);
    MString S(P, Len);
    S = MStringUtil::TrimCopy(S);
    lua_pushlstring(L, S.c_str(), S.size());
    return 1;
}

int FmtToString(lua_State* L)
{
    // 委托 Lua 的 tostring 全局,避免重复实现
    lua_getglobal(L, "tostring");
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    return 1;
}

} // namespace

void MLuaFormat::Install(lua_State* L)
{
    lua_getglobal(L, "Mession");
    if (lua_isnil(L, -1)) {
        lua_newtable(L);
        lua_setglobal(L, "Mession");
        lua_getglobal(L, "Mession");
    }
    lua_newtable(L);
    lua_pushcfunction(L, FmtFmt);     lua_setfield(L, -2, "fmt");
    lua_pushcfunction(L, FmtConcat);  lua_setfield(L, -2, "concat");
    lua_pushcfunction(L, FmtSplit);   lua_setfield(L, -2, "split");
    lua_pushcfunction(L, FmtTrim);    lua_setfield(L, -2, "trim");
    lua_pushcfunction(L, FmtToString);lua_setfield(L, -2, "tostring");
    lua_setfield(L, -2, "Format");
    lua_pop(L, 1);
}

} // namespace mession::script::lua