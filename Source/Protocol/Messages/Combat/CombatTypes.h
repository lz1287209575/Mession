#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

enum class ECombatUnitKind : uint8
{
    Unknown = 0,
    Player = 1,
    Monster = 2,
};

MSTRUCT()
struct FCombatUnitRef
{
    MPROPERTY()
    ECombatUnitKind UnitKind = ECombatUnitKind::Unknown;

    MPROPERTY()
    uint64 CombatEntityId = 0;

    MPROPERTY()
    uint64 PlayerId = 0;

    bool IsValid() const
    {
        return UnitKind != ECombatUnitKind::Unknown && (CombatEntityId != 0 || PlayerId != 0);
    }

    bool IsPlayer() const
    {
        return UnitKind == ECombatUnitKind::Player;
    }

    bool IsMonster() const
    {
        return UnitKind == ECombatUnitKind::Monster;
    }

    static FCombatUnitRef MakePlayer(uint64 InCombatEntityId, uint64 InPlayerId)
    {
        FCombatUnitRef Result;
        Result.UnitKind = ECombatUnitKind::Player;
        Result.CombatEntityId = InCombatEntityId;
        Result.PlayerId = InPlayerId;
        return Result;
    }

    static FCombatUnitRef MakeMonster(uint64 InCombatEntityId)
    {
        FCombatUnitRef Result;
        Result.UnitKind = ECombatUnitKind::Monster;
        Result.CombatEntityId = InCombatEntityId;
        return Result;
    }
};
