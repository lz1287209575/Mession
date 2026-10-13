/**
 * @file FPlayerMessages.h
 * @brief Player 域成员调用消息(ActorMember 框架的验证用例)。
 *
 * 请求继承 FPlayerRequestBase,把 PlayerId 当**显式字段**带上:框架的成员分发器
 * DispatchFrameworkMemberCall 反序列化后 FindProperty("PlayerId"),再按该值把
 * 调用路由到对应 actor 宿主。
 *
 * 设计依据见 Docs/superpowers/specs/2026-08-14-actor-member-framework.md §5。
 *
 * @note 继承序列化是"先父后子",基类字段先落盘(Runtime/Reflect/Class.cpp),
 *       所以接收端反序列化完就能直接读到 PlayerId。
 * @note 这里不做"把实例编码进函数 id"那套做法:实例一律经 Registry 的 ActorIds 定位。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief FPlayerRequestBase - Player 域请求基类,承载寻址用的 PlayerId。
 */
MSTRUCT()
struct FPlayerRequestBase {
    /// 目标玩家 id(显式寻址字段,框架按此路由)
    MPROPERTY()
    uint64 PlayerId = 0;
};

/**
 * @brief FPlayerUseItemRequest - 使用背包物品请求(继承基类字段)。
 *
 * 由 MPlayerItemContainer::UseItem 处理。
 */
MSTRUCT()
struct FPlayerUseItemRequest : FPlayerRequestBase {
    /// 物品 id
    MPROPERTY()
    uint64 ItemId = 0;

    /// 使用数量
    MPROPERTY()
    int32 Count = 0;
};

/**
 * @brief FPlayerUseItemResponse - 使用物品的结果。
 */
MSTRUCT()
struct FPlayerUseItemResponse {
    /// 是否使用成功
    MPROPERTY()
    bool bOk = false;

    /// 回显物品 id,便于客户端关联请求
    MPROPERTY()
    uint64 ItemId = 0;

    /// 使用后的剩余数量;-1 表示物品不存在
    MPROPERTY()
    int32 Remaining = 0;
};
