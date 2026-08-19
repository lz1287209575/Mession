#include "Common/Script/Lua/MLuaFormat.h"
#include "Common/Script/Lua/LuaScriptState.h"

#include <cstdio>
#include <cassert>

using namespace mession::script::lua;

static int PeekInt(lua_State* L, int Idx)
{
    int V = (int)lua_tointeger(L, Idx);
    lua_pop(L, 1);
    return V;
}

static MString PeekString(lua_State* L, int Idx)
{
    size_t      Len = 0;
    const char* P   = lua_tolstring(L, Idx, &Len);
    MString S(P, Len);
    lua_pop(L, 1);
    return S;
}

static void TestFmtBasic()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaFormat::Install(L);

    int RC = luaL_dostring(L,
        "return Mession.Format.fmt('hello {} world', 'lua'), "
        "Mession.Format.fmt('{} + {} = {}', 1, 2, 3)");
    assert(RC == 0);
    assert(PeekString(L, -2) == MString("hello lua world"));
    assert(PeekString(L, -1) == MString("1 + 2 = 3"));
    std::printf("ok: TestFmtBasic\n");
}

static void TestFmtNoArgs()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaFormat::Install(L);

    int RC = luaL_dostring(L, "return Mession.Format.fmt('plain text')");
    assert(RC == 0);
    assert(PeekString(L, -1) == MString("plain text"));
    std::printf("ok: TestFmtNoArgs\n");
}

static void TestConcat()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaFormat::Install(L);

    int RC = luaL_dostring(L, "return Mession.Format.concat('a', 'b', 'c', 'd')");
    assert(RC == 0);
    assert(PeekString(L, -1) == MString("abcd"));
    std::printf("ok: TestConcat\n");
}

static void TestSplit()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaFormat::Install(L);

    int RC = luaL_dostring(L,
        "local parts = Mession.Format.split('a,b,c,d', ',')\n"
        "return #parts, parts[1], parts[2], parts[3], parts[4]");
    assert(RC == 0);
    assert(PeekInt(L, -5) == 4);
    assert(PeekString(L, -4) == MString("a"));
    assert(PeekString(L, -3) == MString("b"));
    assert(PeekString(L, -2) == MString("c"));
    assert(PeekString(L, -1) == MString("d"));
    std::printf("ok: TestSplit\n");
}

static void TestTrim()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaFormat::Install(L);

    int RC = luaL_dostring(L, "return Mession.Format.trim('  hello  ')");
    assert(RC == 0);
    assert(PeekString(L, -1) == MString("hello"));
    std::printf("ok: TestTrim\n");
}

static void TestFmtError()
{
    MLuaScriptState State;
    lua_State* L = State.GetLuaState();
    MLuaFormat::Install(L);

    // 错误的 fmt 语法应抛错
    int RC = luaL_dostring(L,
        "local ok, err = pcall(Mession.Format.fmt, '{:bad}', 'x')\n"
        "return ok, type(err)");
    assert(RC == 0);
    assert(lua_toboolean(L, -2) == 0);
    lua_pop(L, 1);
    size_t      Len = 0;
    const char* P   = lua_tolstring(L, -1, &Len);
    assert(P && MString(P, Len) == MString("string"));
    lua_pop(L, 1);
    std::printf("ok: TestFmtError\n");
}

int main()
{
    TestFmtBasic();
    TestFmtNoArgs();
    TestConcat();
    TestSplit();
    TestTrim();
    TestFmtError();
    std::printf("ALL OK\n");
    return 0;
}