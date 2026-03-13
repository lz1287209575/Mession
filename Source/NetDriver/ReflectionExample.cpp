#include "NetDriver/ReflectionExample.h"
#include "Common/Logger.h"

// ===========================
// MCharacter 实现
// ===========================

void MCharacter::TakeDamage(float Damage)
{
    Health -= Damage;
    if (Health <= 0.0f)
    {
        Health = 0.0f;
        bIsAlive = false;
        LOG_INFO("%s died!", Name.c_str());
    }
}

void MCharacter::Heal(float Amount)
{
    if (!bIsAlive)
    {
        return;
    }

    Health += Amount;
    if (Health > MaxHealth)
    {
        Health = MaxHealth;
    }
}

void MCharacter::MoveTo(const SVector& NewLocation)
{
    Location = NewLocation;
    LOG_DEBUG("%s moved to (%.2f, %.2f, %.2f)",
              Name.c_str(), Location.X, Location.Y, Location.Z);
}

void MCharacter::LevelUp()
{
    Level++;
    MaxHealth += 20.0f;
    Health = MaxHealth;
    LOG_INFO("%s leveled up! Now level %d", Name.c_str(), Level);
}

void MCharacter::Tick(float DeltaTime)
{
    (void)DeltaTime;
}

void MCharacter::BeginPlay()
{
    LOG_INFO("%s spawned at level %d", Name.c_str(), Level);
}

void MCharacter::RegisterAllProperties(MClass* InClass)
{
    REGISTER_PROPERTY(FString, String, Name, EPropertyFlags::None);
    REGISTER_PROPERTY(int32, Int32, Level, EPropertyFlags::None);
    REGISTER_PROPERTY(int64, Int64, Experience, EPropertyFlags::None);
    REGISTER_PROPERTY(SVector, Vector, Location, EPropertyFlags::None);
    REGISTER_PROPERTY(SRotator, Rotator, Rotation, EPropertyFlags::None);
    REGISTER_PROPERTY(float, Float, Health, EPropertyFlags::None);
    REGISTER_PROPERTY(float, Float, MaxHealth, EPropertyFlags::None);
    REGISTER_PROPERTY(float, Float, MoveSpeed, EPropertyFlags::None);
    REGISTER_PROPERTY(bool, Bool, bIsAlive, EPropertyFlags::None);
    REGISTER_PROPERTY(int32, Int32, Gold, EPropertyFlags::None);
}

void MCharacter::RegisterAllFunctions(MClass* InClass)
{
    MFunction* Func = new MFunction();
    Func->Name = "LevelUp";
    Func->Flags = EFunctionFlags::None;
    Func->NativeFunc = [](void* Obj)
    {
        if (!Obj)
        {
            return;
        }
        auto* Character = static_cast<MCharacter*>(Obj);
        Character->LevelUp();
    };
    InClass->RegisterFunction(Func);
}

IMPLEMENT_CLASS(MCharacter, MReflectObject, 0)

// ===========================
// MHero 实现（包含嵌套结构体）
// ===========================

void MHero::ServerRpc_AddStats(int32 LevelDelta, float HealthDelta)
{
    Level += LevelDelta;
    Health += HealthDelta;
}

bool MHero::ServerRpc_AddStats_Validate(int32 LevelDelta, float HealthDelta) const
{
    // 简单校验：不允许负值或过大增益
    if (LevelDelta < 0 || HealthDelta < 0.0f)
    {
        return false;
    }
    if (LevelDelta > 100 || HealthDelta > 10000.0f)
    {
    return false;
    }
    return true;
}

namespace
{
// 非捕获函数指针，用于演示 RPC 反序列化 + 调用
void Hero_ServerRpcAddStats_Invoker(MReflectObject* Object, MReflectArchive& Ar)
{
    if (!Object)
    {
        return;
    }

    auto* Hero = static_cast<MHero*>(Object);
    int32 LevelDelta = 0;
    float HealthDelta = 0.0f;
    Ar << LevelDelta;
    Ar << HealthDelta;
    if (!Hero->ServerRpc_AddStats_Validate(LevelDelta, HealthDelta))
    {
        LOG_WARN("ServerRpc_AddStats_Validate failed: LevelDelta=%d HealthDelta=%.2f",
                 LevelDelta, HealthDelta);
        return;
    }
    Hero->ServerRpc_AddStats(LevelDelta, HealthDelta);
}
}

void MHero::RegisterAllProperties(MClass* InClass)
{
    REGISTER_PROPERTY(FString, String, Name, EPropertyFlags::None);
    // 使用 EPropertyType::Struct，对嵌套结构体整体按字节序列化
    REGISTER_PROPERTY(SCombatStats, Struct, CombatStats, EPropertyFlags::None);
    REGISTER_PROPERTY(int32, Int32, Level, EPropertyFlags::None);
    REGISTER_PROPERTY(float, Float, Health, EPropertyFlags::None);
}

void MHero::RegisterAllFunctions(MClass* InClass)
{
    // 注册一个带参数的服务器 RPC 示例
    MFunction* Func = new MFunction();
    Func->Name = "ServerRpc_AddStats";
    Func->Flags = EFunctionFlags::NetServer;
    Func->RpcType = ERpcType::Server;
    Func->bReliable = true;
    Func->RpcFunc = &Hero_ServerRpcAddStats_Invoker;
    InClass->RegisterFunction(Func);
}

IMPLEMENT_CLASS(MHero, MReflectObject, 0)

// ===========================
// MPlayerData 实现
// ===========================

void MPlayerData::AddFriend(uint64 FriendId)
{
    if (!HasFriend(FriendId))
    {
        FriendsList.push_back(FriendId);
    }
}

void MPlayerData::RemoveFriend(uint64 FriendId)
{
    for (auto It = FriendsList.begin(); It != FriendsList.end(); ++It)
    {
        if (*It == FriendId)
        {
            FriendsList.erase(It);
            break;
        }
    }
}

bool MPlayerData::HasFriend(uint64 FriendId) const
{
    for (uint64 Id : FriendsList)
    {
        if (Id == FriendId)
        {
            return true;
        }
    }
    return false;
}

void MPlayerData::RegisterAllProperties(MClass* InClass)
{
    REGISTER_PROPERTY(uint64, UInt64, PlayerId, EPropertyFlags::None);
    REGISTER_PROPERTY(FString, String, AccountName, EPropertyFlags::None);
    REGISTER_PROPERTY(int32, Int32, VIPLevel, EPropertyFlags::None);
    REGISTER_PROPERTY(int64, Int64, LoginTime, EPropertyFlags::None);
    REGISTER_PROPERTY(FString, String, LastLoginIP, EPropertyFlags::None);
    REGISTER_PROPERTY(TVector<uint64>, Array, FriendsList, EPropertyFlags::None);
    REGISTER_PROPERTY(FriendLevelMap, Array, FriendLevels, EPropertyFlags::None);
    REGISTER_PROPERTY(BlackListSet, Array, BlackList, EPropertyFlags::None);
}

void MPlayerData::RegisterAllFunctions(MClass* InClass)
{
    (void)InClass;
}

IMPLEMENT_CLASS(MPlayerData, MReflectObject, 0)

