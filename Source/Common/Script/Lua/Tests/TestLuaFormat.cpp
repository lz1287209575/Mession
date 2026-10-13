/**
 * @file TestLuaFormat.cpp
 * @brief 验证 Mession.Format 的 fmt / concat / split / trim 入口。
 *
 * @detail 本文件内的 PeekInt / PeekString 用于读取任意栈槽位而不破坏其它槽位。
 */
#include "Common/Script/Lua/LuaScriptState.h"
#include "Common/Script/Lua/MLuaFormat.h"

#include <cassert>
#include <cstdio>

using namespace mession::script::lua;

/**
 * @brief PeekInt - 读取任意栈槽位的整数,且不改变该槽位。
 * @param L   目标 Lua 状态机
 * @param Idx 栈索引
 * @return 该槽位的整数值
 *
 * @detail 实现为 pushvalue(Idx) → 读栈顶 → pop。旧实现直接 lua_pop(L, 1),
 *   永远弹栈顶:当 Idx != -1(如 -2)时会弹掉别的槽位,导致后续读取错位
 *   (TestFmtBasic 断言失败的根因)。
 */
static int PeekInt(lua_State* L, int Idx) {
    lua_pushvalue(L, Idx);
    int V = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return V;
}

/** @brief PeekString - 读取任意栈槽位的字符串(按长度拷贝),且不改变该槽位 */
static MString PeekString(lua_State* L, int Idx) {
    lua_pushvalue(L, Idx);
    size_t      Len = 0;
    const char* P   = lua_tolstring(L, -1, &Len);
    MString     S(P, Len);
    lua_pop(L, 1);
    return S;
}

/** @brief TestFmtBasic - fmt 占位符替换(含单实参与多实参两种) */
static void TestFmtBasic() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaFormat::Install(L);

    int RC = luaL_dostring(L, "return Mession.Format.fmt('hello {} world', 'lua'), "
                              "Mession.Format.fmt('{} + {} = {}', 1, 2, 3)");
    assert(RC == 0);
    assert(PeekString(L, -2) == MString("hello lua world"));
    assert(PeekString(L, -1) == MString("1 + 2 = 3"));
    std::printf("ok: TestFmtBasic\n");
}

/** @brief TestFmtNoArgs - 无实参时原样返回模板串 */
static void TestFmtNoArgs() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaFormat::Install(L);

    int RC = luaL_dostring(L, "return Mession.Format.fmt('plain text')");
    assert(RC == 0);
    assert(PeekString(L, -1) == MString("plain text"));
    std::printf("ok: TestFmtNoArgs\n");
}

/** @brief TestConcat - concat 依次拼接 4 个字符串 */
static void TestConcat() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaFormat::Install(L);

    int RC = luaL_dostring(L, "return Mession.Format.concat('a', 'b', 'c', 'd')");
    assert(RC == 0);
    assert(PeekString(L, -1) == MString("abcd"));
    std::printf("ok: TestConcat\n");
}

/** @brief TestSplit - split 按逗号切分出 4 段并保持原顺序 */
static void TestSplit() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaFormat::Install(L);

    int RC = luaL_dostring(L, "local parts = Mession.Format.split('a,b,c,d', ',')\n"
                              "return #parts, parts[1], parts[2], parts[3], parts[4]");
    assert(RC == 0);
    assert(PeekInt(L, -5) == 4);
    assert(PeekString(L, -4) == MString("a"));
    assert(PeekString(L, -3) == MString("b"));
    assert(PeekString(L, -2) == MString("c"));
    assert(PeekString(L, -1) == MString("d"));
    std::printf("ok: TestSplit\n");
}

/** @brief TestTrim - trim 去掉首尾空白 */
static void TestTrim() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaFormat::Install(L);

    int RC = luaL_dostring(L, "return Mession.Format.trim('  hello  ')");
    assert(RC == 0);
    assert(PeekString(L, -1) == MString("hello"));
    std::printf("ok: TestTrim\n");
}

/** @brief TestFmtError - 非法 fmt 语法应被 pcall 捕获为 string 型错误 */
static void TestFmtError() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaFormat::Install(L);

    // 错误的 fmt 语法应抛错
    int RC = luaL_dostring(L, "local ok, err = pcall(Mession.Format.fmt, '{:bad}', 'x')\n"
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

/**
 * @brief main - 依次执行 6 个用例。
 * @return 恒为 0(断言失败会直接 abort)
 */
int main() {
    TestFmtBasic();
    TestFmtNoArgs();
    TestConcat();
    TestSplit();
    TestTrim();
    TestFmtError();
    std::printf("ALL OK\n");
    return 0;
}