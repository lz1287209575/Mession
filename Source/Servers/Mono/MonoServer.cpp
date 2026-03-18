#include "MonoServer.h"
#include "Common/ReflectionSmokeTypes.h"
#include "Common/ServerConnection.h"
#include "NetDriver/Reflection.h"
#include "Messages/NetMessages.h"

namespace
{
struct SRenameParams
{
    FString NewName;
};

struct SGetGoldAmountParams
{
    int32 ReturnValue = 0;
};

struct SIsAliveParams
{
    bool ReturnValue = false;
};

struct SHasFriendParams
{
    uint64 FriendId = 0;
    bool ReturnValue = false;
};

bool RunReflectionTests()
{
    LOG_INFO("=== MonoServer: Running reflection tests ===");
    
    // 测试 1：MReflectionSmokeCharacter 属性反射、函数调用与序列化
    {
        MClass* CharacterClass = MReflectionSmokeCharacter::StaticClass();
        if (!CharacterClass)
        {
            LOG_ERROR("MReflectionSmokeCharacter::StaticClass returned nullptr");
            return false;
        }
        
        auto* Character = static_cast<MReflectionSmokeCharacter*>(CharacterClass->CreateInstance());
        if (!Character)
        {
            LOG_ERROR("Failed to CreateInstance for MReflectionSmokeCharacter");
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
            LOG_ERROR("Failed to get one or more properties from MReflectionSmokeCharacter");
            return false;
        }
        
        const int32 OldLevel = *LevelPtr;
        
        LOG_INFO("MReflectionSmokeCharacter before serialize: Level=%d Health=%.2f Gold=%d",
                 *LevelPtr, *HealthPtr, *GoldPtr);
        
        // 通过反射调用无参函数 LevelUp
        if (!Character->CallFunction("LevelUp"))
        {
            LOG_ERROR("CallFunction(LevelUp) failed on MReflectionSmokeCharacter");
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
        LOG_INFO("MReflectionSmokeCharacter after CallFunction(LevelUp): Level=%d", *LevelAfterCall);

        if (!Character->CallFunctionArgs("Rename", FString("ReflectedHero")))
        {
            LOG_ERROR("CallFunctionArgs(Rename) failed on MReflectionSmokeCharacter");
            return false;
        }

        FString* NameAfterRename = GET_PROPERTY(Character, FString, Name);
        if (!NameAfterRename || *NameAfterRename != "ReflectedHero")
        {
            LOG_ERROR("Rename via reflection did not update Name");
            return false;
        }

        int32 GoldByReturn = 0;
        if (!Character->CallFunctionWithReturn("GetGoldAmount", GoldByReturn))
        {
            LOG_ERROR("CallFunctionWithReturn(GetGoldAmount) failed on MReflectionSmokeCharacter");
            return false;
        }
        if (GoldByReturn != 123)
        {
            LOG_ERROR("GetGoldAmount via reflection returned unexpected value: %d", GoldByReturn);
            return false;
        }

        bool bIsAlive = false;
        if (!Character->CallFunctionWithReturn("IsAlive", bIsAlive))
        {
            LOG_ERROR("CallFunctionWithReturn(IsAlive) failed on MReflectionSmokeCharacter");
            return false;
        }
        if (!bIsAlive)
        {
            LOG_ERROR("IsAlive via reflection returned false unexpectedly");
            return false;
        }

        SRenameParams RenameParams;
        RenameParams.NewName = "ProcessEventHero";
        if (!Character->ProcessEvent("Rename", &RenameParams))
        {
            LOG_ERROR("ProcessEvent(Rename) failed on MReflectionSmokeCharacter");
            return false;
        }

        NameAfterRename = GET_PROPERTY(Character, FString, Name);
        if (!NameAfterRename || *NameAfterRename != "ProcessEventHero")
        {
            LOG_ERROR("ProcessEvent(Rename) did not update Name");
            return false;
        }

        SGetGoldAmountParams GoldParams;
        if (!Character->ProcessEvent("GetGoldAmount", &GoldParams))
        {
            LOG_ERROR("ProcessEvent(GetGoldAmount) failed on MReflectionSmokeCharacter");
            return false;
        }
        if (GoldParams.ReturnValue != 123)
        {
            LOG_ERROR("ProcessEvent(GetGoldAmount) returned unexpected value: %d", GoldParams.ReturnValue);
            return false;
        }

        SIsAliveParams AliveParams;
        if (!Character->ProcessEvent("IsAlive", &AliveParams))
        {
            LOG_ERROR("ProcessEvent(IsAlive) failed on MReflectionSmokeCharacter");
            return false;
        }
        if (!AliveParams.ReturnValue)
        {
            LOG_ERROR("ProcessEvent(IsAlive) returned false unexpectedly");
            return false;
        }

        MEnum* ArchetypeEnum = MReflectObject::FindEnum("EReflectionSmokeArchetype");
        if (!ArchetypeEnum)
        {
            LOG_ERROR("EReflectionSmokeArchetype enum was not auto-registered");
            return false;
        }
        const MEnumValue* MageValue = ArchetypeEnum->FindValue("Mage");
        if (!MageValue || MageValue->Value != 2)
        {
            LOG_ERROR("EReflectionSmokeArchetype enum metadata is invalid");
            return false;
        }
        LOG_INFO("Reflected enum EReflectionSmokeArchetype registered with %zu values",
                 static_cast<size_t>(ArchetypeEnum->GetValues().size()));
        
        // 序列化
        MReflectArchive ArWrite;
        CharacterClass->WriteSnapshot(Character, ArWrite);
        
        auto* CharacterCopy = static_cast<MReflectionSmokeCharacter*>(CharacterClass->CreateInstance());
        if (!CharacterCopy)
        {
            LOG_ERROR("Failed to CreateInstance for MReflectionSmokeCharacter copy");
            return false;
        }
        CharacterClass->ReadSnapshot(CharacterCopy, ArWrite.Data);
        
        int32* LevelCopy = GET_PROPERTY(CharacterCopy, int32, Level);
        float* HealthCopy = GET_PROPERTY(CharacterCopy, float, Health);
        int32* GoldCopy = GET_PROPERTY(CharacterCopy, int32, Gold);
        
        if (!LevelCopy || !HealthCopy || !GoldCopy)
        {
            LOG_ERROR("Failed to get one or more properties from MReflectionSmokeCharacter copy");
            return false;
        }
        
        LOG_INFO("MReflectionSmokeCharacter after deserialize: Level=%d Health=%.2f Gold=%d",
                 *LevelCopy, *HealthCopy, *GoldCopy);
    }
    
    // 测试 2：MReflectionSmokePlayerData 容器序列化（FriendsList / FriendLevels / BlackList）
    {
        MClass* PlayerDataClass = MReflectionSmokePlayerData::StaticClass();
        if (!PlayerDataClass)
        {
            LOG_ERROR("MReflectionSmokePlayerData::StaticClass returned nullptr");
            return false;
        }
        
        auto* Data = static_cast<MReflectionSmokePlayerData*>(PlayerDataClass->CreateInstance());
        if (!Data)
        {
            LOG_ERROR("Failed to CreateInstance for MReflectionSmokePlayerData");
            return false;
        }
        
        // 通过反射接口设置基本字段
        SET_PROPERTY(Data, uint64, PlayerId, 1001);
        SET_PROPERTY(Data, FString, AccountName, FString("TestAccount"));
        SET_PROPERTY(Data, int32, VIPLevel, 3);
        SET_PROPERTY(Data, int64, LoginTime, 123456789);
        SET_PROPERTY(Data, FString, LastLoginIP, FString("127.0.0.1"));

        // 直接操作容器字段（通过普通 C++ 访问）
        Data->GetFriendsList() = {2001, 2002, 2003};
        Data->GetFriendLevels().clear();
        Data->GetFriendLevels()[2001] = 10;
        Data->GetFriendLevels()[2002] = 20;
        Data->GetFriendLevels()[2003] = 30;

        Data->GetBlackList().clear();
        Data->GetBlackList().insert(3001);
        Data->GetBlackList().insert(3002);

        if (!Data->CallFunctionArgs("AddFriend", static_cast<uint64>(2004)))
        {
            LOG_ERROR("CallFunctionArgs(AddFriend) failed on MReflectionSmokePlayerData");
            return false;
        }
        bool bHasFriend = false;
        if (!Data->CallFunctionWithReturn("HasFriend", bHasFriend, static_cast<uint64>(2004)))
        {
            LOG_ERROR("CallFunctionWithReturn(HasFriend) failed on MReflectionSmokePlayerData");
            return false;
        }
        if (!bHasFriend)
        {
            LOG_ERROR("HasFriend via reflection returned false unexpectedly");
            return false;
        }

        SHasFriendParams HasFriendParams;
        HasFriendParams.FriendId = 2004;
        if (!Data->ProcessEvent("HasFriend", &HasFriendParams))
        {
            LOG_ERROR("ProcessEvent(HasFriend) failed on MReflectionSmokePlayerData");
            return false;
        }
        if (!HasFriendParams.ReturnValue)
        {
            LOG_ERROR("ProcessEvent(HasFriend) returned false unexpectedly");
            return false;
        }
        
        uint64* PlayerIdPtr = GET_PROPERTY(Data, uint64, PlayerId);
        FString* AccountNamePtr = GET_PROPERTY(Data, FString, AccountName);
        int32* VipLevelPtr = GET_PROPERTY(Data, int32, VIPLevel);
        
        LOG_INFO("MReflectionSmokePlayerData before serialize: PlayerId=%llu Account=%s VIP=%d Friends=%zu FriendLevels=%zu BlackList=%zu",
                 PlayerIdPtr ? (unsigned long long)(*PlayerIdPtr) : 0ull,
                 AccountNamePtr ? AccountNamePtr->c_str() : "",
                 VipLevelPtr ? *VipLevelPtr : -1,
                 (size_t)Data->GetFriendsList().size(),
                 (size_t)Data->GetFriendLevels().size(),
                 (size_t)Data->GetBlackList().size());
        
        TArray Buffer;
        {
            MReflectArchive Ar;
            PlayerDataClass->WriteSnapshot(Data, Ar);
            Buffer = Ar.Data;
        }
        
        auto* DataCopy = static_cast<MReflectionSmokePlayerData*>(PlayerDataClass->CreateInstance());
        if (!DataCopy)
        {
            LOG_ERROR("Failed to CreateInstance for MReflectionSmokePlayerData copy");
            return false;
        }
        PlayerDataClass->ReadSnapshot(DataCopy, Buffer);
        
        uint64* PlayerIdCopyPtr = GET_PROPERTY(DataCopy, uint64, PlayerId);
        FString* AccountNameCopyPtr = GET_PROPERTY(DataCopy, FString, AccountName);
        int32* VipLevelCopyPtr = GET_PROPERTY(DataCopy, int32, VIPLevel);
        
        LOG_INFO("MReflectionSmokePlayerData after deserialize: PlayerId=%llu Account=%s VIP=%d Friends=%zu FriendLevels=%zu BlackList=%zu",
                 PlayerIdCopyPtr ? (unsigned long long)(*PlayerIdCopyPtr) : 0ull,
                 AccountNameCopyPtr ? AccountNameCopyPtr->c_str() : "",
                 VipLevelCopyPtr ? *VipLevelCopyPtr : -1,
                 (size_t)DataCopy->GetFriendsList().size(),
                 (size_t)DataCopy->GetFriendLevels().size(),
                 (size_t)DataCopy->GetBlackList().size());

        if (DataCopy->GetFriendsList().size() != 4 ||
            DataCopy->GetFriendLevels().size() != 3 ||
            DataCopy->GetBlackList().size() != 2)
        {
            LOG_ERROR("MReflectionSmokePlayerData container serialization mismatch after deserialize");
            return false;
        }
    }

    // 测试 3：MReflectionSmokeHero 嵌套结构体（Struct 类型）自动序列化
    {
        MClass* HeroClass = MReflectionSmokeHero::StaticClass();
        if (!HeroClass)
        {
            LOG_ERROR("MReflectionSmokeHero::StaticClass returned nullptr");
            return false;
        }

        auto* Hero = static_cast<MReflectionSmokeHero*>(HeroClass->CreateInstance());
        if (!Hero)
        {
            LOG_ERROR("Failed to CreateInstance for MReflectionSmokeHero");
            return false;
        }

        // 通过反射拿到嵌套结构体整体指针
        SReflectionSmokeCombatStats* StatsPtr = GET_PROPERTY(Hero, SReflectionSmokeCombatStats, CombatStats);
        if (!StatsPtr)
        {
            LOG_ERROR("Failed to get CombatStats property from MReflectionSmokeHero");
            return false;
        }

        // 设置多层嵌套字段
        StatsPtr->Base.Strength = 10;
        StatsPtr->Base.Agility = 5;
        StatsPtr->Base.Intelligence = 3;
        StatsPtr->Bonus.Strength = 2;
        StatsPtr->Bonus.Agility = 1;
        StatsPtr->Bonus.Intelligence = 0;
        StatsPtr->CritChance = 0.25f;
        StatsPtr->CritMultiplier = 2.0f;

        SET_PROPERTY(Hero, int32, Level, 15);
        SET_PROPERTY(Hero, float, Health, 350.0f);

        LOG_INFO("MReflectionSmokeHero before serialize: "
                 "Base(STR=%d AGI=%d INT=%d) "
                 "Bonus(STR=%d AGI=%d INT=%d) "
                 "Crit=%.2f x%.2f Level=%d Health=%.2f",
                 StatsPtr->Base.Strength,
                 StatsPtr->Base.Agility,
                 StatsPtr->Base.Intelligence,
                 StatsPtr->Bonus.Strength,
                 StatsPtr->Bonus.Agility,
                 StatsPtr->Bonus.Intelligence,
                 StatsPtr->CritChance,
                 StatsPtr->CritMultiplier,
                 Hero->GetLevel(),
                 Hero->GetHealth());

        // 通过 MClass 通用接口序列化 / 反序列化
        MReflectArchive HeroArWrite;
        HeroClass->WriteSnapshot(Hero, HeroArWrite);

        auto* HeroCopy = static_cast<MReflectionSmokeHero*>(HeroClass->CreateInstance());
        if (!HeroCopy)
        {
            LOG_ERROR("Failed to CreateInstance for MReflectionSmokeHero copy");
            return false;
        }
        HeroClass->ReadSnapshot(HeroCopy, HeroArWrite.Data);

        SReflectionSmokeCombatStats* StatsCopyPtr = GET_PROPERTY(HeroCopy, SReflectionSmokeCombatStats, CombatStats);
        if (!StatsCopyPtr)
        {
            LOG_ERROR("Failed to get CombatStats from MReflectionSmokeHero copy");
            return false;
        }

        LOG_INFO("MReflectionSmokeHero after deserialize: "
                 "Base(STR=%d AGI=%d INT=%d) "
                 "Bonus(STR=%d AGI=%d INT=%d) "
                 "Crit=%.2f x%.2f Level=%d Health=%.2f",
                 StatsCopyPtr->Base.Strength,
                 StatsCopyPtr->Base.Agility,
                 StatsCopyPtr->Base.Intelligence,
                 StatsCopyPtr->Bonus.Strength,
                 StatsCopyPtr->Bonus.Agility,
                 StatsCopyPtr->Bonus.Intelligence,
                 StatsCopyPtr->CritChance,
                 StatsCopyPtr->CritMultiplier,
                 HeroCopy->GetLevel(),
                 HeroCopy->GetHealth());
    }

    // 测试 4：RPC 元信息与网络包格式（本地自发自收模拟）
    {
        MClass* HeroClass = MReflectionSmokeHero::StaticClass();
        auto* Hero = static_cast<MReflectionSmokeHero*>(HeroClass->CreateInstance());
        if (!Hero)
        {
            LOG_ERROR("Failed to CreateInstance for MReflectionSmokeHero in RPC test");
            return false;
        }

        // 查找 RPC 函数元信息
        MFunction* RpcFuncMeta = HeroClass->FindFunction("ServerRpc_AddStats");
        if (!RpcFuncMeta)
        {
            LOG_ERROR("RPC metadata for ServerRpc_AddStats not found");
            return false;
        }

        const uint64 ObjectId = Hero->GetId();
        const uint16 FunctionId = RpcFuncMeta->FunctionId;

        // 构造 RPC 参数载荷
        int32 LevelDelta = 2;
        float HealthDelta = 50.0f;
        MReflectArchive RpcPayloadAr;
        RpcPayloadAr << LevelDelta;
        RpcPayloadAr << HealthDelta;

        const uint32 PayloadSize = static_cast<uint32>(RpcPayloadAr.Data.size());

        // 按 EServerMessageType::MT_RPC 设计网络包格式：
        // [MsgType(1)][ObjectId(8)][FunctionId(2)][PayloadSize(4)][Payload...]
        TArray Packet;
        Packet.reserve(1 + sizeof(ObjectId) + sizeof(FunctionId) + sizeof(PayloadSize) + PayloadSize);

        uint8 MsgType = static_cast<uint8>(EServerMessageType::MT_RPC);
        Packet.push_back(MsgType);

        // ObjectId
        const uint8* ObjIdPtr = reinterpret_cast<const uint8*>(&ObjectId);
        Packet.insert(Packet.end(), ObjIdPtr, ObjIdPtr + sizeof(ObjectId));

        // FunctionId
        const uint8* FuncIdPtr = reinterpret_cast<const uint8*>(&FunctionId);
        Packet.insert(Packet.end(), FuncIdPtr, FuncIdPtr + sizeof(FunctionId));

        // PayloadSize
        const uint8* SizePtr = reinterpret_cast<const uint8*>(&PayloadSize);
        Packet.insert(Packet.end(), SizePtr, SizePtr + sizeof(PayloadSize));

        // Payload 本体
        if (PayloadSize > 0)
        {
            Packet.insert(Packet.end(), RpcPayloadAr.Data.begin(), RpcPayloadAr.Data.end());
        }

        LOG_INFO("RPC packet built: size=%zu bytes (payload=%u)", (size_t)Packet.size(), PayloadSize);

        // 本地“接收端”解析同一份 Packet，并通过 RpcFunc 执行
        size_t Offset = 0;
        if (Packet.empty())
        {
            LOG_ERROR("RPC packet is empty");
            return false;
        }

        uint8 RecvType = Packet[Offset++];
        if (RecvType != static_cast<uint8>(EServerMessageType::MT_RPC))
        {
            LOG_ERROR("Unexpected RPC msg type: %u", (unsigned)RecvType);
            return false;
        }

        if (Offset + sizeof(uint64) + sizeof(uint16) + sizeof(uint32) > Packet.size())
        {
            LOG_ERROR("RPC packet too small");
            return false;
        }

        uint64 RecvObjectId = 0;
        uint16 RecvFunctionId = 0;
        uint32 RecvPayloadSize = 0;

        std::memcpy(&RecvObjectId, Packet.data() + Offset, sizeof(RecvObjectId));
        Offset += sizeof(RecvObjectId);

        std::memcpy(&RecvFunctionId, Packet.data() + Offset, sizeof(RecvFunctionId));
        Offset += sizeof(RecvFunctionId);

        std::memcpy(&RecvPayloadSize, Packet.data() + Offset, sizeof(RecvPayloadSize));
        Offset += sizeof(RecvPayloadSize);

        if (Offset + RecvPayloadSize > Packet.size())
        {
            LOG_ERROR("RPC packet payload out of range");
            return false;
        }

        TArray RecvPayload;
        if (RecvPayloadSize > 0)
        {
            RecvPayload.resize(RecvPayloadSize);
            std::memcpy(RecvPayload.data(), Packet.data() + Offset, RecvPayloadSize);
        }

        LOG_INFO("RPC packet parsed: ObjectId=%llu FunctionId=%u PayloadSize=%u",
                 (unsigned long long)RecvObjectId,
                 (unsigned)RecvFunctionId,
                 (unsigned)RecvPayloadSize);

        // 使用统一反射入口执行序列化 RPC 调用（此处直接用 Hero 实例模拟）
        MReflectArchive RecvAr(RecvPayload);
        if (!Hero->InvokeSerializedFunction(RpcFuncMeta, RecvAr))
        {
            LOG_ERROR("InvokeSerializedFunction failed for ServerRpc_AddStats");
            return false;
        }

        LOG_INFO("MReflectionSmokeHero after RPC invoke: Level=%d Health=%.2f",
                 Hero->GetLevel(),
                 Hero->GetHealth());
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
