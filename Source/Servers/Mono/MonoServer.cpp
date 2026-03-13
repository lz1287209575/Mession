#include "MonoServer.h"
#include "NetDriver/ReflectionExample.h"
#include "NetDriver/Reflection.h"

namespace
{
bool RunReflectionTests()
{
    LOG_INFO("=== MonoServer: Running reflection tests ===");
    
    // 测试 1：MCharacter 属性反射、函数调用与序列化
    {
        MClass* CharacterClass = MCharacter::StaticClass();
        if (!CharacterClass)
        {
            LOG_ERROR("MCharacter::StaticClass returned nullptr");
            return false;
        }
        
        auto* Character = static_cast<MCharacter*>(CharacterClass->CreateInstance());
        if (!Character)
        {
            LOG_ERROR("Failed to CreateInstance for MCharacter");
            return false;
        }
        
        // 设置属性
        SET_PROPERTY(Character, int32, Level, 10);
        SET_PROPERTY(Character, float, Health, 50.0f);
        SET_PROPERTY(Character, int32, Gold, 123);
        
        int32* LevelPtr = GET_PROPERTY(Character, int32, Level);
        float* HealthPtr = GET_PROPERTY(Character, float, Health);
        int32* GoldPtr = GET_PROPERTY(Character, int32, Gold);
        
        if (!LevelPtr || !HealthPtr || !GoldPtr)
        {
            LOG_ERROR("Failed to get one or more properties from MCharacter");
            return false;
        }
        
        const int32 OldLevel = *LevelPtr;
        
        LOG_INFO("MCharacter before serialize: Level=%d Health=%.2f Gold=%d",
                 *LevelPtr, *HealthPtr, *GoldPtr);
        
        // 通过反射调用无参函数 LevelUp
        if (!Character->CallFunction("LevelUp"))
        {
            LOG_ERROR("CallFunction(LevelUp) failed on MCharacter");
            return false;
        }
        
        int32* LevelAfterCall = GET_PROPERTY(Character, int32, Level);
        if (!LevelAfterCall || *LevelAfterCall != OldLevel + 1)
        {
            LOG_ERROR("LevelUp via reflection did not change Level as expected (before=%d after=%d)",
                      OldLevel,
                      LevelAfterCall ? *LevelAfterCall : -1);
            return false;
        }
        LOG_INFO("MCharacter after CallFunction(LevelUp): Level=%d", *LevelAfterCall);
        
        // 序列化
        MReflectArchive ArWrite;
        CharacterClass->Serialize(Character, ArWrite);
        
        auto* CharacterCopy = static_cast<MCharacter*>(CharacterClass->CreateInstance());
        if (!CharacterCopy)
        {
            LOG_ERROR("Failed to CreateInstance for MCharacter copy");
            return false;
        }
        CharacterClass->Deserialize(CharacterCopy, ArWrite.Data);
        
        int32* LevelCopy = GET_PROPERTY(CharacterCopy, int32, Level);
        float* HealthCopy = GET_PROPERTY(CharacterCopy, float, Health);
        int32* GoldCopy = GET_PROPERTY(CharacterCopy, int32, Gold);
        
        if (!LevelCopy || !HealthCopy || !GoldCopy)
        {
            LOG_ERROR("Failed to get one or more properties from MCharacter copy");
            return false;
        }
        
        LOG_INFO("MCharacter after deserialize: Level=%d Health=%.2f Gold=%d",
                 *LevelCopy, *HealthCopy, *GoldCopy);
    }
    
    // 测试 2：MPlayerData 自定义序列化（包含 FriendsList）
    {
        MClass* PlayerDataClass = MPlayerData::StaticClass();
        if (!PlayerDataClass)
        {
            LOG_ERROR("MPlayerData::StaticClass returned nullptr");
            return false;
        }
        
        auto* Data = static_cast<MPlayerData*>(PlayerDataClass->CreateInstance());
        if (!Data)
        {
            LOG_ERROR("Failed to CreateInstance for MPlayerData");
            return false;
        }
        
        // 通过反射接口设置基本字段
        SET_PROPERTY(Data, uint64, PlayerId, 1001);
        SET_PROPERTY(Data, FString, AccountName, FString("TestAccount"));
        SET_PROPERTY(Data, int32, VIPLevel, 3);
        SET_PROPERTY(Data, int64, LoginTime, 123456789);
        SET_PROPERTY(Data, FString, LastLoginIP, FString("127.0.0.1"));
        
        uint64* PlayerIdPtr = GET_PROPERTY(Data, uint64, PlayerId);
        FString* AccountNamePtr = GET_PROPERTY(Data, FString, AccountName);
        int32* VipLevelPtr = GET_PROPERTY(Data, int32, VIPLevel);
        
        LOG_INFO("MPlayerData before serialize: PlayerId=%llu Account=%s VIP=%d",
                 PlayerIdPtr ? (unsigned long long)(*PlayerIdPtr) : 0ull,
                 AccountNamePtr ? AccountNamePtr->c_str() : "",
                 VipLevelPtr ? *VipLevelPtr : -1);
        
        TArray Buffer;
        {
            MReflectArchive Ar;
            PlayerDataClass->Serialize(Data, Ar);
            Buffer = Ar.Data;
        }
        
        auto* DataCopy = static_cast<MPlayerData*>(PlayerDataClass->CreateInstance());
        if (!DataCopy)
        {
            LOG_ERROR("Failed to CreateInstance for MPlayerData copy");
            return false;
        }
        PlayerDataClass->Deserialize(DataCopy, Buffer);
        
        uint64* PlayerIdCopyPtr = GET_PROPERTY(DataCopy, uint64, PlayerId);
        FString* AccountNameCopyPtr = GET_PROPERTY(DataCopy, FString, AccountName);
        int32* VipLevelCopyPtr = GET_PROPERTY(DataCopy, int32, VIPLevel);
        
        LOG_INFO("MPlayerData after deserialize: PlayerId=%llu Account=%s VIP=%d",
                 PlayerIdCopyPtr ? (unsigned long long)(*PlayerIdCopyPtr) : 0ull,
                 AccountNameCopyPtr ? AccountNameCopyPtr->c_str() : "",
                 VipLevelCopyPtr ? *VipLevelCopyPtr : -1);
    }
    
    LOG_INFO("=== MonoServer: Reflection tests finished ===");
    return true;
}
}

bool MMonoServer::Init()
{
    LOG_INFO("MonoServer Init");
    return true;
}

bool MMonoServer::Run()
{
    LOG_INFO("MonoServer Run - starting tests");
    const bool bOk = RunReflectionTests();
    if (bOk)
    {
        LOG_INFO("MonoServer tests succeeded");
    }
    else
    {
        LOG_ERROR("MonoServer tests failed");
    }
    return bOk;
}

