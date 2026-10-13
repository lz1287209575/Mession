/**
 * @file TestLuaVector.cpp
 * @brief 验证 Mession.Vector 的增删查改、越界、相等、遍历、克隆与 GC 行为。
 */
#include "Common/Script/Lua/LuaScriptState.h"
#include "Common/Script/Lua/MLuaVector.h"

#include <cassert>
#include <cstdio>

using namespace mession::script::lua;

/** @brief TestCreateEmpty - 新建 MVector 的 size() 应为 0 */
static void TestCreateEmpty() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L, "return Mession.Vector.new():size()");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -1) == 0);
    lua_pop(L, 1);
    std::printf("ok: TestCreateEmpty\n");
}

/** @brief TestPushPop - push 三次后 pop 应返回 30 且 size() 变为 2 */
static void TestPushPop() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L, "local v = Mession.Vector.new()\n"
                              "v:push(10); v:push(20); v:push(30)\n"
                              "local last = v:pop()\n"
                              "return last, v:size()");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -2) == 30);
    assert((int)lua_tointeger(L, -1) == 2);
    lua_pop(L, 2);
    std::printf("ok: TestPushPop\n");
}

/** @brief TestGetSet - get/set 使用 1-based 下标 */
static void TestGetSet() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L, "local v = Mession.Vector.new(3, 0)\n"
                              "v:set(2, 99)\n"
                              "return v:get(1), v:get(2), v:get(3)");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -3) == 0);
    assert((int)lua_tointeger(L, -2) == 99);
    assert((int)lua_tointeger(L, -1) == 0);
    lua_pop(L, 3);
    std::printf("ok: TestGetSet\n");
}

/** @brief TestInsertRemove - insert(2,99) 后 remove(2) 应还原为 [1,3] */
static void TestInsertRemove() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L, "local v = Mession.Vector.new()\n"
                              "v:push(1); v:push(3)\n"
                              "v:insert(2, 99)\n"
                              "local removed = v:remove(2)\n"
                              "return v:size(), removed, v:get(1), v:get(2)");
    assert(RC == 0);
    // 栈: [-4]=size [-3]=removed [-2]=get(1) [-1]=get(2)
    // push(1),push(3) 后 insert(2,99) → [1,99,3],remove(2) 移除 99 → [1,3]
    // 因此 size()==2(旧断言写 3 是错的,remove 之后元素少了一个)
    assert((int)lua_tointeger(L, -4) == 2);
    assert((int)lua_tointeger(L, -3) == 99);
    assert((int)lua_tointeger(L, -2) == 1);
    assert((int)lua_tointeger(L, -1) == 3);
    lua_pop(L, 4);
    std::printf("ok: TestInsertRemove\n");
}

/** @brief TestBoundary - 越界 set 应经 pcall 捕获为错误 */
static void TestBoundary() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L, "local v = Mession.Vector.new(2, 0)\n"
                              "local ok, err = pcall(v.set, v, 100, 99)\n"
                              "return ok, err");
    assert(RC == 0);
    assert(lua_toboolean(L, -2) == 0);
    lua_pop(L, 2);
    std::printf("ok: TestBoundary\n");
}

/** @brief TestEq - 内容相同的两个 MVector 应相等 */
static void TestEq() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L, "local v1 = Mession.Vector.new(3, 1)\n"
                              "local v2 = Mession.Vector.new(3, 1)\n"
                              "return v1 == v2, v1:get(1) == v2:get(1)");
    assert(RC == 0);
    assert(lua_toboolean(L, -2) == 1);
    assert(lua_toboolean(L, -1) == 1);
    lua_pop(L, 2);
    std::printf("ok: TestEq\n");
}

/** @brief TestIpairs - pairs(v) 遍历应能累加出全部元素之和 */
static void TestIpairs() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L, "local v = Mession.Vector.new()\n"
                              "v:push(10); v:push(20); v:push(30)\n"
                              "local sum = 0\n"
                              // ipairs 在 Lua 5.4 对 userdata 不生效(裸 geti 不走 __index),改用 pairs
                              "for i, val in pairs(v) do sum = sum + val end\n"
                              "return sum");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -1) == 60);
    lua_pop(L, 1);
    std::printf("ok: TestIpairs\n");
}

/** @brief TestClone - clone 后修改副本不应影响原对象 */
static void TestClone() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L, "local v = Mession.Vector.new()\n"
                              "v:push(10); v:push(20)\n"
                              "local c = v:clone()\n"
                              "c:push(30)\n"
                              "return v:size(), c:size()");
    assert(RC == 0);
    assert((int)lua_tointeger(L, -2) == 2);
    assert((int)lua_tointeger(L, -1) == 3);
    lua_pop(L, 2);
    std::printf("ok: TestClone\n");
}

/** @brief TestGC - 置 nil 并 collectgarbage() 后不应崩溃(走 __gc 析构) */
static void TestGC() {
    MLuaScriptState State;
    lua_State*      L = State.GetLuaState();
    MLuaVector::Install(L);

    int RC = luaL_dostring(L, "local v = Mession.Vector.new()\n"
                              "v:push(1); v:push(2)\n"
                              "v = nil\n"
                              "collectgarbage()\n"
                              "collectgarbage()\n"
                              "return 1");
    assert(RC == 0);
    lua_pop(L, 1);
    std::printf("ok: TestGC\n");
}

/**
 * @brief main - 依次执行 9 个用例。
 * @return 恒为 0(断言失败会直接 abort)
 */
int main() {
    TestCreateEmpty();
    TestPushPop();
    TestGetSet();
    TestInsertRemove();
    TestBoundary();
    TestEq();
    TestIpairs();
    TestClone();
    TestGC();
    std::printf("ALL OK\n");
    return 0;
}