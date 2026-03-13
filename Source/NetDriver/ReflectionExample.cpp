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

void MPlayerData::Serialize(void* Object, MReflectArchive& Ar) const
{
    auto* Data = static_cast<MPlayerData*>(Object);
    Ar << Data->PlayerId;
    Ar << Data->AccountName;
    Ar << Data->VIPLevel;
    Ar << Data->LoginTime;
    Ar << Data->LastLoginIP;

    uint32 FriendCount = static_cast<uint32>(Data->FriendsList.size());
    Ar << FriendCount;
    for (uint64 Fid : Data->FriendsList)
    {
        Ar << Fid;
    }
}

void MPlayerData::Deserialize(void* Object, const TArray& Data) const
{
    auto* PlayerData = static_cast<MPlayerData*>(Object);
    MReflectArchive Ar(Data);
    Ar << PlayerData->PlayerId;
    Ar << PlayerData->AccountName;
    Ar << PlayerData->VIPLevel;
    Ar << PlayerData->LoginTime;
    Ar << PlayerData->LastLoginIP;

    uint32 FriendCount = 0;
    Ar << FriendCount;
    PlayerData->FriendsList.resize(FriendCount);
    for (uint32 i = 0; i < FriendCount; ++i)
    {
        Ar << PlayerData->FriendsList[i];
    }
}

void MPlayerData::RegisterAllProperties(MClass* InClass)
{
    REGISTER_PROPERTY(uint64, UInt64, PlayerId, EPropertyFlags::None);
    REGISTER_PROPERTY(FString, String, AccountName, EPropertyFlags::None);
    REGISTER_PROPERTY(int32, Int32, VIPLevel, EPropertyFlags::None);
    REGISTER_PROPERTY(int64, Int64, LoginTime, EPropertyFlags::None);
    REGISTER_PROPERTY(FString, String, LastLoginIP, EPropertyFlags::None);
}

void MPlayerData::RegisterAllFunctions(MClass* InClass)
{
    (void)InClass;
}

IMPLEMENT_CLASS(MPlayerData, MReflectObject, 0)

