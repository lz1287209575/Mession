#pragma once

#include "Common/Runtime/MLib.h"

enum class ESkillNodeCategory : uint8
{
    Flow = 0,
    Targeting = 1,
    Validation = 2,
    Effect = 3,
    Terminal = 4,
};

enum class ESkillServerOp : uint8
{
#define MESSION_SKILL_NODE(OpName, StableNodeIdValue, RegistryNameLiteral, DisplayNameLiteral, CategoryValue, EditorNodeTokenLiteral, MinOutgoingEdgesValue, MaxOutgoingEdgesValue, UsesSkillTargetTypeValue, FloatParam0KeyLiteral, FloatParam0DisplayNameLiteral, FloatParam0RequiredValue, Float0Semantic, FloatParam1KeyLiteral, FloatParam1DisplayNameLiteral, FloatParam1RequiredValue, Float1Semantic, IntParam0KeyLiteral, IntParam0DisplayNameLiteral, IntParam0RequiredValue, NameParamKeyLiteral, NameParamDisplayNameLiteral, NameParamRequiredValue, StringParamKeyLiteral, StringParamDisplayNameLiteral, StringParamRequiredValue) \
    OpName,
#include "Common/Skill/SkillNodeRegistry.def"
#undef MESSION_SKILL_NODE
    Count,
};

enum class ESkillNodeFloatSemantic : uint8
{
    None = 0,
    RequiredRange = 1,
    BaseDamage = 2,
    AttackPowerScale = 3,
};

enum class ESkillNodeParamValueType : uint8
{
    None = 0,
    Float = 1,
    Int = 2,
    Name = 3,
    String = 4,
};

struct FSkillNodeParamSlotSchema
{
    TStringView Key;
    TStringView DisplayName;
    ESkillNodeParamValueType ValueType = ESkillNodeParamValueType::None;
    bool bRequired = false;
    ESkillNodeFloatSemantic FloatSemantic = ESkillNodeFloatSemantic::None;
};

struct FSkillNodeRegistryEntry
{
    uint16 StableNodeId = 0;
    ESkillServerOp ServerOp = ESkillServerOp::Start;
    TStringView RegistryName;
    TStringView DisplayName;
    ESkillNodeCategory Category = ESkillNodeCategory::Flow;
    TStringView EditorNodeToken;
    uint8 MinOutgoingEdges = 0;
    uint8 MaxOutgoingEdges = 0;
    bool bUsesSkillTargetType = false;
    FSkillNodeParamSlotSchema FloatParam0;
    FSkillNodeParamSlotSchema FloatParam1;
    FSkillNodeParamSlotSchema IntParam0;
    FSkillNodeParamSlotSchema NameParam;
    FSkillNodeParamSlotSchema StringParam;
};

inline constexpr FSkillNodeRegistryEntry GSkillNodeRegistry[] = {
#define MESSION_SKILL_NODE(OpName, StableNodeIdValue, RegistryNameLiteral, DisplayNameLiteral, CategoryValue, EditorNodeTokenLiteral, MinOutgoingEdgesValue, MaxOutgoingEdgesValue, UsesSkillTargetTypeValue, FloatParam0KeyLiteral, FloatParam0DisplayNameLiteral, FloatParam0RequiredValue, Float0Semantic, FloatParam1KeyLiteral, FloatParam1DisplayNameLiteral, FloatParam1RequiredValue, Float1Semantic, IntParam0KeyLiteral, IntParam0DisplayNameLiteral, IntParam0RequiredValue, NameParamKeyLiteral, NameParamDisplayNameLiteral, NameParamRequiredValue, StringParamKeyLiteral, StringParamDisplayNameLiteral, StringParamRequiredValue) \
    FSkillNodeRegistryEntry { \
        StableNodeIdValue, \
        ESkillServerOp::OpName, \
        RegistryNameLiteral, \
        DisplayNameLiteral, \
        ESkillNodeCategory::CategoryValue, \
        EditorNodeTokenLiteral, \
        MinOutgoingEdgesValue, \
        MaxOutgoingEdgesValue, \
        UsesSkillTargetTypeValue, \
        FSkillNodeParamSlotSchema { \
            FloatParam0KeyLiteral, \
            FloatParam0DisplayNameLiteral, \
            ESkillNodeParamValueType::Float, \
            FloatParam0RequiredValue, \
            ESkillNodeFloatSemantic::Float0Semantic}, \
        FSkillNodeParamSlotSchema { \
            FloatParam1KeyLiteral, \
            FloatParam1DisplayNameLiteral, \
            ESkillNodeParamValueType::Float, \
            FloatParam1RequiredValue, \
            ESkillNodeFloatSemantic::Float1Semantic}, \
        FSkillNodeParamSlotSchema { \
            IntParam0KeyLiteral, \
            IntParam0DisplayNameLiteral, \
            ESkillNodeParamValueType::Int, \
            IntParam0RequiredValue, \
            ESkillNodeFloatSemantic::None}, \
        FSkillNodeParamSlotSchema { \
            NameParamKeyLiteral, \
            NameParamDisplayNameLiteral, \
            ESkillNodeParamValueType::Name, \
            NameParamRequiredValue, \
            ESkillNodeFloatSemantic::None}, \
        FSkillNodeParamSlotSchema { \
            StringParamKeyLiteral, \
            StringParamDisplayNameLiteral, \
            ESkillNodeParamValueType::String, \
            StringParamRequiredValue, \
            ESkillNodeFloatSemantic::None}},
#include "Common/Skill/SkillNodeRegistry.def"
#undef MESSION_SKILL_NODE
};

inline constexpr size_t GSkillNodeRegistryCount = sizeof(GSkillNodeRegistry) / sizeof(GSkillNodeRegistry[0]);

static_assert(static_cast<size_t>(ESkillServerOp::Count) == GSkillNodeRegistryCount);

inline const FSkillNodeRegistryEntry* FindSkillNodeRegistryEntry(ESkillServerOp ServerOp)
{
    const size_t Index = static_cast<size_t>(ServerOp);
    if (Index >= GSkillNodeRegistryCount)
    {
        return nullptr;
    }

    return &GSkillNodeRegistry[Index];
}

inline bool IsSkillNodeOutgoingEdgeCountValid(const FSkillNodeRegistryEntry& Entry, size_t EdgeCount)
{
    return EdgeCount >= Entry.MinOutgoingEdges && EdgeCount <= Entry.MaxOutgoingEdges;
}

inline const FSkillNodeRegistryEntry* FindSkillNodeRegistryEntryByStableNodeId(uint16 StableNodeId)
{
    for (const FSkillNodeRegistryEntry& Entry : GSkillNodeRegistry)
    {
        if (Entry.StableNodeId == StableNodeId)
        {
            return &Entry;
        }
    }

    return nullptr;
}

inline const FSkillNodeRegistryEntry* FindSkillNodeRegistryEntryByRegistryName(TStringView RegistryName)
{
    for (const FSkillNodeRegistryEntry& Entry : GSkillNodeRegistry)
    {
        if (Entry.RegistryName == RegistryName)
        {
            return &Entry;
        }
    }

    return nullptr;
}

inline const FSkillNodeRegistryEntry* FindSkillNodeRegistryEntryByEditorType(TStringView NodeTypeName)
{
    for (const FSkillNodeRegistryEntry& Entry : GSkillNodeRegistry)
    {
        if (Entry.EditorNodeToken.empty())
        {
            continue;
        }

        if (NodeTypeName.find(Entry.EditorNodeToken) != TStringView::npos)
        {
            return &Entry;
        }
    }

    return nullptr;
}
