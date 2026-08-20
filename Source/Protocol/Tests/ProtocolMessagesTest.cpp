// ProtocolMessagesTest — #5 协议样例 round-trip 验证。
// 真实形状消息（战斗/场景）：多字段、嵌套结构、数组、枚举、bool、
// 浮点坐标——BuildPayload → ParsePayload 往返后逐字段断言。

#include "Common/Net/Rpc/RpcPayload.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/FBattleMessages.h"
#include "Protocol/Messages/Scene/FSceneMessages.h"

#include <cassert>
#include <cstdio>

static void TestBattleDamageRoundTrip() {
    FBattleDamageNotify In;
    In.BattleId        = 9001;
    In.Attacker.UnitId = 101;
    In.Attacker.Name   = "Warrior";
    In.Attacker.Hp     = 100;
    In.Attacker.MaxHp  = 120;
    In.Attacker.Effects.push_back(FStatusEffect{EEffectType::Burn, 3.5f, 2});
    In.Attacker.Effects.push_back(FStatusEffect{EEffectType::Shield, 10.0f, 1});
    In.Defender.UnitId   = 202;
    In.Defender.Name     = "Mage";
    In.Defender.Hp       = 60;
    In.Defender.MaxHp    = 80;
    In.Defender.Effects.push_back(FStatusEffect{EEffectType::Poison, 5.0f, 3});
    In.Damage     = 12345;
    In.bCritical  = true;

    const TByteArray Bytes = BuildPayload(In);
    assert(!Bytes.empty());

    FBattleDamageNotify Out;
    const auto Result = ParsePayload(Bytes, Out);
    assert(Result.IsOk());
    assert(Out.BattleId == 9001);
    assert(Out.Attacker.UnitId == 101);
    assert(Out.Attacker.Name == "Warrior");
    assert(Out.Attacker.Hp == 100 && Out.Attacker.MaxHp == 120);
    assert(Out.Attacker.Effects.size() == 2);
    assert(Out.Attacker.Effects[0].Type == EEffectType::Burn);
    assert(Out.Attacker.Effects[0].Duration == 3.5f);
    assert(Out.Attacker.Effects[0].Stack == 2);
    assert(Out.Attacker.Effects[1].Type == EEffectType::Shield);
    assert(Out.Defender.UnitId == 202);
    assert(Out.Defender.Name == "Mage");
    assert(Out.Defender.Effects.size() == 1);
    assert(Out.Defender.Effects[0].Type == EEffectType::Poison);
    assert(Out.Damage == 12345);
    assert(Out.bCritical);
    std::printf("ok: TestBattleDamageRoundTrip (%zu bytes, effects 2+1)\n", Bytes.size());
}

static void TestSceneEnterRoundTrip() {
    FSceneEnterMsg In;
    In.PlayerId = 555;
    In.SceneId  = "scene_001";
    In.Position = FVector3{1.5f, 2.5f, 3.5f};
    In.Rotation = FVector3{0.0f, 90.0f, 0.0f};
    FActorSpawnInfo A1;
    A1.ActorId  = 1001;
    A1.Prefab   = "NPC_Guard";
    A1.Position = FVector3{10.0f, 0.0f, 20.0f};
    FActorSpawnInfo A2;
    A2.ActorId  = 1002;
    A2.Prefab   = "Chest_Common";
    A2.Position = FVector3{-5.0f, 1.0f, 8.0f};
    In.Actors.push_back(A1);
    In.Actors.push_back(A2);

    const TByteArray Bytes = BuildPayload(In);
    assert(!Bytes.empty());

    FSceneEnterMsg Out;
    const auto Result = ParsePayload(Bytes, Out);
    assert(Result.IsOk());
    assert(Out.PlayerId == 555);
    assert(Out.SceneId == "scene_001");
    assert(Out.Position.X == 1.5f && Out.Position.Y == 2.5f && Out.Position.Z == 3.5f);
    assert(Out.Rotation.Y == 90.0f);
    assert(Out.Actors.size() == 2);
    assert(Out.Actors[0].ActorId == 1001);
    assert(Out.Actors[0].Prefab == "NPC_Guard");
    assert(Out.Actors[0].Position.X == 10.0f);
    assert(Out.Actors[1].ActorId == 1002);
    assert(Out.Actors[1].Prefab == "Chest_Common");
    std::printf("ok: TestSceneEnterRoundTrip (%zu bytes, actors 2)\n", Bytes.size());
}

static void TestEnumReflection() {
    // 战斗消息里的 namespace 级 scoped enum 应能被反射查询（#5 附带验证）
    MEnum* Enum = MObject::FindEnum("EEffectType");
    assert(Enum != nullptr);
    assert(Enum->GetValues().size() >= 5);
    std::printf("ok: TestEnumReflection (FindEnum EEffectType, %zu values)\n", Enum->GetValues().size());
}

int main() {
    TestBattleDamageRoundTrip();
    TestSceneEnterRoundTrip();
    TestEnumReflection();
    std::printf("ALL OK\n");
    return 0;
}
