#include "Common/Script/Lua/MLuaVector.h"
#include "Common/Runtime/Log/Log.h"

#include <new>

namespace mession::script::lua {

TResult<MScalarValue> MScalarValue::FromLua(lua_State* L, int32 Index)
{
    int Type = lua_type(L, Index);
    switch (Type)
    {
    case LUA_TNIL:
        return TResult<MScalarValue>::Ok(MScalarValue::MakeInt(0));
    case LUA_TBOOLEAN:
        return TResult<MScalarValue>::Ok(MScalarValue::MakeInt(lua_toboolean(L, Index) ? 1 : 0));
    case LUA_TNUMBER:
        return TResult<MScalarValue>::Ok(MScalarValue::MakeDouble(lua_tonumber(L, Index)));
    case LUA_TSTRING:
    {
        size_t Len = 0;
        const char* P = lua_tolstring(L, Index, &Len);
        return TResult<MScalarValue>::Ok(MScalarValue::MakeString(MString(P, Len)));
    }
    default:
        return TResult<MScalarValue>::Err(MString("unsupported_type"));
    }
}

void MScalarValue::PushToLua(lua_State* L) const
{
    switch (Type)
    {
    case MScalarType::Int:
        lua_pushinteger(L, IntVal);
        break;
    case MScalarType::Double:
        lua_pushnumber(L, DoubleVal);
        break;
    case MScalarType::String:
        lua_pushlstring(L, StringVal.c_str(), StringVal.size());
        break;
    }
}

MString MScalarValue::ToDebugString() const
{
    switch (Type)
    {
    case MScalarType::Int:    return MString("Int(") + std::to_string(IntVal) + MString(")");
    case MScalarType::Double: return MString("Double(") + std::to_string(DoubleVal) + MString(")");
    case MScalarType::String: return MString("String(") + StringVal + MString(")");
    }
    return MString("Unknown");
}

namespace {

TVector<MScalarValue>* GetVector(lua_State* L, int32 ArgNum = 1)
{
    return static_cast<TVector<MScalarValue>*>(luaL_checkudata(L, ArgNum, "MVectorProxy"));
}

inline const char* MVectorProxyMetaName = "MVectorProxy";

int MetaLen(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    lua_pushinteger(L, static_cast<int32>(V->size()));
    return 1;
}

int MetaToString(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    MString S = MString("Vector(size=") + std::to_string(V->size()) + MString(")");
    lua_pushlstring(L, S.c_str(), S.size());
    return 1;
}

int MetaEq(lua_State* L)
{
    TVector<MScalarValue>* A = GetVector(L, 1);
    TVector<MScalarValue>* B = GetVector(L, 2);
    if (A->size() != B->size()) { lua_pushboolean(L, 0); return 1; }
    for (size_t i = 0; i < A->size(); ++i)
    {
        if (!((*A)[i] == (*B)[i])) { lua_pushboolean(L, 0); return 1; }
    }
    lua_pushboolean(L, 1);
    return 1;
}

int MetaPairs(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    int* Idx = static_cast<int*>(lua_newuserdata(L, sizeof(int)));
    *Idx = 0;
    luaL_newmetatable(L, "MVectorIter");
    lua_setmetatable(L, -2);
    lua_pushvalue(L, 1);
    lua_pushcfunction(L, [](lua_State* L) -> int {
        TVector<MScalarValue>* V2 = static_cast<TVector<MScalarValue>*>(lua_touserdata(L, lua_upvalueindex(1)));
        int* I2 = static_cast<int*>(lua_touserdata(L, lua_upvalueindex(2)));
        if (*I2 >= static_cast<int>(V2->size())) return 0;
        lua_pushinteger(L, (*I2) + 1);
        (*V2)[*I2].PushToLua(L);
        (*I2)++;
        return 2;
    });
    lua_insert(L, -3);
    return 3;
}

/// @lua-stdlib Vector.get
/// @lua-self MVector
/// @lua-param i integer
/// @lua-return MScalar
int MethodGet(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    int32 Idx = static_cast<int32>(luaL_checkinteger(L, 2));
    if (Idx < 1 || Idx > static_cast<int32>(V->size()))
    {
        return luaL_error(L, "out_of_range");
    }
    (*V)[Idx - 1].PushToLua(L);
    return 1;
}

/// @lua-stdlib Vector.set
/// @lua-self MVector
/// @lua-param i integer
/// @lua-param v MScalar
/// @lua-return nil
int MethodSet(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    int32 Idx = static_cast<int32>(luaL_checkinteger(L, 2));
    if (Idx < 1 || Idx > static_cast<int32>(V->size()))
    {
        return luaL_error(L, "out_of_range");
    }
    auto Scalar = MScalarValue::FromLua(L, 3);
    if (Scalar.IsErr())
    {
        return luaL_error(L, "unsupported_type");
    }
    (*V)[Idx - 1] = Scalar.GetValue();
    return 0;
}

/// @lua-stdlib Vector.push
/// @lua-self MVector
/// @lua-param v MScalar
/// @lua-return nil
int MethodPush(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    auto Scalar = MScalarValue::FromLua(L, 2);
    if (Scalar.IsErr())
    {
        return luaL_error(L, "unsupported_type");
    }
    V->push_back(Scalar.GetValue());
    return 0;
}

/// @lua-stdlib Vector.pop
/// @lua-self MVector
/// @lua-return MScalar|nil
int MethodPop(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    if (V->empty())
    {
        lua_pushnil(L);
        return 1;
    }
    MScalarValue Out = V->back();
    V->pop_back();
    Out.PushToLua(L);
    return 1;
}

/// @lua-stdlib Vector.size
/// @lua-self MVector
/// @lua-return integer
int MethodSize(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    lua_pushinteger(L, static_cast<int32>(V->size()));
    return 1;
}

/// @lua-stdlib Vector.insert
/// @lua-self MVector
/// @lua-param i integer
/// @lua-param v MScalar
/// @lua-return nil
int MethodInsert(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    int32 Idx = static_cast<int32>(luaL_checkinteger(L, 2));
    if (Idx < 1 || Idx > static_cast<int32>(V->size()) + 1)
    {
        return luaL_error(L, "out_of_range");
    }
    auto Scalar = MScalarValue::FromLua(L, 3);
    if (Scalar.IsErr())
    {
        return luaL_error(L, "unsupported_type");
    }
    V->insert(V->begin() + (Idx - 1), Scalar.GetValue());
    return 0;
}

/// @lua-stdlib Vector.remove
/// @lua-self MVector
/// @lua-param i integer
/// @lua-return MScalar
int MethodRemove(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    int32 Idx = static_cast<int32>(luaL_checkinteger(L, 2));
    if (Idx < 1 || Idx > static_cast<int32>(V->size()))
    {
        return luaL_error(L, "out_of_range");
    }
    MScalarValue Out = (*V)[Idx - 1];
    V->erase(V->begin() + (Idx - 1));
    Out.PushToLua(L);
    return 1;
}

/// @lua-stdlib Vector.clear
/// @lua-self MVector
/// @lua-return nil
int MethodClear(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    V->clear();
    return 0;
}

/// @lua-stdlib Vector.clone
/// @lua-self MVector
/// @lua-return MVector
int MethodClone(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    void* Ud = lua_newuserdata(L, sizeof(TVector<MScalarValue>));
    new (Ud) TVector<MScalarValue>(*V);
    luaL_setmetatable(L, MVectorProxyMetaName);
    return 1;
}

int MetaGC(lua_State* L)
{
    TVector<MScalarValue>* V = GetVector(L, 1);
    V->~TVector<MScalarValue>();
    return 0;
}

/// @lua-stdlib Vector.new
/// @lua-param size? integer
/// @lua-param init? MScalar
/// @lua-return MVector
int FactoryNew(lua_State* L)
{
    int32 Size = 0;
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1))
    {
        Size = static_cast<int32>(luaL_checkinteger(L, 1));
        if (Size < 0)
        {
            return luaL_error(L, "size_must_be_non_negative");
        }
    }

    void* Ud = lua_newuserdata(L, sizeof(TVector<MScalarValue>));
    auto V = new (Ud) TVector<MScalarValue>();
    V->reserve(Size);
    if (Size > 0 && lua_gettop(L) >= 2 && !lua_isnil(L, 2))
    {
        auto Init = MScalarValue::FromLua(L, 2);
        if (Init.IsErr())
        {
            return luaL_error(L, "unsupported_type");
        }
        for (int32 i = 0; i < Size; ++i)
        {
            V->push_back(Init.GetValue());
        }
    }

    luaL_setmetatable(L, MVectorProxyMetaName);
    return 1;
}

void RegisterMetaMethods(lua_State* L)
{
    luaL_newmetatable(L, MVectorProxyMetaName);

    lua_pushcfunction(L, MetaLen);                  lua_setfield(L, -2, "__len");
    lua_pushcfunction(L, MetaToString);             lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, MetaEq);                   lua_setfield(L, -2, "__eq");
    lua_pushcfunction(L, MetaPairs);                 lua_setfield(L, -2, "__pairs");
    lua_pushcfunction(L, MetaGC);                   lua_setfield(L, -2, "__gc");

    lua_newtable(L);
    lua_pushcfunction(L, MethodGet);                lua_setfield(L, -2, "get");
    lua_pushcfunction(L, MethodSet);                lua_setfield(L, -2, "set");
    lua_pushcfunction(L, MethodPush);               lua_setfield(L, -2, "push");
    lua_pushcfunction(L, MethodPop);                lua_setfield(L, -2, "pop");
    lua_pushcfunction(L, MethodSize);               lua_setfield(L, -2, "size");
    lua_pushcfunction(L, MethodInsert);             lua_setfield(L, -2, "insert");
    lua_pushcfunction(L, MethodRemove);             lua_setfield(L, -2, "remove");
    lua_pushcfunction(L, MethodClear);              lua_setfield(L, -2, "clear");
    lua_pushcfunction(L, MethodClone);              lua_setfield(L, -2, "clone");
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

} // namespace

void MLuaVector::Install(lua_State* L)
{
    RegisterMetaMethods(L);

    lua_getglobal(L, "Mession");
    if (lua_isnil(L, -1))
    {
        lua_newtable(L);
        lua_setglobal(L, "Mession");
        lua_getglobal(L, "Mession");
    }
    lua_newtable(L);
    lua_pushcfunction(L, FactoryNew);
    lua_setfield(L, -2, "new");
    lua_setfield(L, -2, "Vector");
    lua_pop(L, 1);
}

} // namespace mession::script::lua
