#include "Common/Net/Rpc/RpcDispatch.h"

#include "Common/Runtime/Log/Log.h"

namespace
{
struct SClientEntry
{
    MClass* OwnerClass = nullptr;
    MFunction* Function = nullptr;
};

bool FindClientEntryByFunctionId(
    const MClass* TargetClass,
    uint16 FunctionId,
    SClientEntry& OutEntry)
{
    if (!TargetClass || FunctionId == 0)
    {
        return false;
    }

    MFunction* Function = const_cast<MClass*>(TargetClass)->FindFunctionById(FunctionId);
    if (!Function)
    {
        return false;
    }

    OutEntry.OwnerClass = const_cast<MClass*>(TargetClass);
    OutEntry.Function = Function;
    return true;
}
} // namespace

const MFunction* FindClientFunctionById(const MClass* TargetClass, uint16 FunctionId)
{
    SClientEntry Entry;
    if (!FindClientEntryByFunctionId(TargetClass, FunctionId, Entry))
    {
        return nullptr;
    }

    return Entry.Function;
}