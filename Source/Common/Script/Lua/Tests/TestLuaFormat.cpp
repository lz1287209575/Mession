#include "Common/Script/Lua/MLuaFormat.h"
#include "Common/Script/Lua/LuaScriptState.h"

#include <cstdio>
#include <cassert>

using namespace mession::script::lua;

// 把 Idx 槽位复制到栈顶,读副本后 pop —— 保证 pop 掉的正是读过的值。
// 旧实现 lua_pop(L, 1) 永远弹栈顶: 当 Idx != -1 时(如 -2)会弹掉别的槽位,
// 导致后续 PeekString(L, -1) 读到错误值(TestFmtBasic 断言失败的根因)。
static int PeekInt(lua_State* L, int Idx)
{
    lua_pushvalue(L, Idx);
    int V = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return V;
}

static MString PeekString(lua_State* L, int Idx)
{
    lua_pushvalue(L, Idx);
    size_t      Len = 0;
    const char* P   = lua_tolstring(L, -1, &Len);
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
    // 栈: [-2]=pcall ok(false) [-1]=type(err)("string")
    // 先读 -2 再读 -1,最后一次性 pop(2) —— 旧写法中途 pop(1) 会弹掉未读的值。
    assert(lua_toboolean(L, -2) == 0);
    size_t      Len = 0;
    const char* P   = lua_tolstring(L, -1, &Len);
    assert(P && MString(P, Len) == MString("string"));
    lua_pop(L, 2);
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