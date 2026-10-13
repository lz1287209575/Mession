/**
 * @file FBattleMessages.h
 * @brief 战斗域协议消息 - 多字段 / 嵌套结构 / 数组 / 枚举的真实形状样例。
 *
 * 这些 MSTRUCT 覆盖 scoped enum、嵌套结构、结构数组、bool 与有符号整数,并作为
 * 反射序列化的 round-trip 验证输入:BuildPayload → ParsePayload 之后逐字段断言
 * 还原(见 Protocol/Tests/ProtocolMessagesTest.cpp)。
 *
 * @note 字段构成 MPROPERTY 序列化形状(对外协议 ABI),增删 / 重排必须与客户端同步。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief 状态效果类型。
 *
 * namespace 级 `enum class : int32`(不是 MSTRUCT 内嵌 enum),同时用于验证枚举
 * 反射注册:调用 MObject::FindEnum("EEffectType") 能查到全部取值。
 */
enum class EEffectType : int32 {
    None   = 0,
    Burn   = 1,
    Poison = 2,
    Stun   = 3,
    Shield = 4,
};

/**
 * @brief 单个状态效果。
 */
MSTRUCT()
struct FStatusEffect {
    /// 效果类型;None 表示该槽位未使用(结构体默认值)
    MPROPERTY()
    EEffectType Type = EEffectType::None;

    /// 剩余持续时间,单位秒(浮点,允许小数)
    MPROPERTY()
    float Duration = 0.0f;

    /// 叠加层数
    MPROPERTY()
    int32 Stack = 0;
};

/**
 * @brief 战斗单元 - 参与战斗的一方。
 *
 * 内含 TVector<FStatusEffect>,用于验证嵌套结构数组能整段序列化并逐元素还原。
 */
MSTRUCT()
struct FCombatUnit {
    /// 单元全局 id
    MPROPERTY()
    uint64 UnitId = 0;

    /// 显示名
    MPROPERTY()
    MString Name;

    /// 当前血量
    MPROPERTY()
    int32 Hp = 0;

    /// 血量上限;与 Hp 不等时客户端据此画血条
    MPROPERTY()
    int32 MaxHp = 0;

    /// 挂载的状态效果列表,可为空
    MPROPERTY()
    TVector<FStatusEffect> Effects;
};

/**
 * @brief 战斗伤害通知 - 业务下行消息示例(攻方 / 守方 / 伤害 / 暴击)。
 */
MSTRUCT()
struct FBattleDamageNotify {
    /// 战斗会话 id
    MPROPERTY()
    uint64 BattleId = 0;

    /// 攻击方快照(含其状态效果)
    MPROPERTY()
    FCombatUnit Attacker;

    /// 防御方快照(含其状态效果)
    MPROPERTY()
    FCombatUnit Defender;

    /// 本次伤害值;用 int64 而非 int32,以免暴击伤害溢出
    MPROPERTY()
    int64 Damage = 0;

    /// 是否暴击(沿用 UE 风格 b 前缀布尔命名)
    MPROPERTY()
    bool bCritical = false;
};
