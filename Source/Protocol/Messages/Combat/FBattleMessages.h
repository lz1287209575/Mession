#pragma once

// FBattleMessages.h — 战斗域真实形状消息（#5 协议样例：多字段/嵌套结构/数组/
// 枚举，验证反射序列化对复杂消息的 round-trip）。

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

// 状态效果类型（namespace 级 scoped enum——同时验证 enum 反射注册）
enum class EEffectType : int32 {
    None   = 0,
    Burn   = 1,
    Poison = 2,
    Stun   = 3,
    Shield = 4,
};

// 单个状态效果
MSTRUCT()
struct FStatusEffect {
    MPROPERTY()
    EEffectType Type = EEffectType::None;

    MPROPERTY()
    float Duration = 0.0f;

    MPROPERTY()
    int32 Stack = 0;
};

// 战斗单元（含嵌套结构数组）
MSTRUCT()
struct FCombatUnit {
    MPROPERTY()
    uint64 UnitId = 0;

    MPROPERTY()
    MString Name;

    MPROPERTY()
    int32 Hp = 0;

    MPROPERTY()
    int32 MaxHp = 0;

    MPROPERTY()
    TVector<FStatusEffect> Effects;
};

// 战斗伤害通知（业务下行示例：攻方/守方/伤害/暴击）
MSTRUCT()
struct FBattleDamageNotify {
    MPROPERTY()
    uint64 BattleId = 0;

    MPROPERTY()
    FCombatUnit Attacker;

    MPROPERTY()
    FCombatUnit Defender;

    MPROPERTY()
    int64 Damage = 0;

    MPROPERTY()
    bool bCritical = false;
};
