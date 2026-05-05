#pragma once

#include "Common/Runtime/MLib.h"

/**
 * MEvent - 零成本事件基类
 *
 * 用继承链注册事件类型，每个事件类型有一个唯一 EventTypeId。
 * 通过模板链式继承实现，零虚函数、无 RTTI 依赖。
 *
 * 设计参考：ET 的 IMessage / ET 的 EventSystem
 * 关键点：每个事件子类定义静态 EventTypeId，无运行时注册开销。
 *
 * 用法示例：
 *
 *   // 1. 定义事件（继承链末尾为 MEvent 基类）
 *   struct FPlayerLoginEvent : MEvent<FPlayerLoginEvent> {
 *       uint64 PlayerId;
 *       uint32 Level;
 *   };
 *
 *   // 2. 获取类型 ID（编译期确定）
 *   uint32 TypeId = FPlayerLoginEvent::GetStaticEventTypeId();
 *
 *   // 3. 发布时
 *   FPlayerLoginEvent e{ .PlayerId = 1, .Level = 10 };
 *   EventBus.Publish(&e);
 *
 * 为什么用继承链而不是 static map：
 * - static map 需要运行时登记，容易遗漏
 * - 继承链的 EventTypeId 由模板参数决定，编译器保证正确
 * - 每个事件类型只有一个全局唯一的 uint32 ID
 */

namespace MEventDetail
{
    // 每个事件类型有一个全局自增 ID，模板参数 T 仅用于区分类型
    template<typename T>
    uint32 GetEventTypeIdImpl()
    {
        static const uint32 Id = []() -> uint32 {
            static uint32 NextId = 1;  // 从 1 开始，0 表示无效
            return NextId++;
        }();
        return Id;
    }
}

/**
 * 事件基类模板：用户事件继承链的末端
 *
 * 使用方式：struct FMyEvent : MEvent<FMyEvent> { ... };
 *
 * T 是事件类本身，用于产生唯一的 EventTypeId。
 */
template<typename TEvent>
struct MEvent
{
    using TEventType = TEvent;

    // 静态获取此事件类型的唯一 ID（每个 TEvent 全局唯一）
    static uint32 GetStaticEventTypeId()
    {
        return MEventDetail::GetEventTypeIdImpl<TEvent>();
    }

    // 虚析构函数（通常不需要，但保证安全）
    virtual ~MEvent() = default;

    // 获取本实例的运行时类型 ID（用于 EventBus 路由）
    uint32 GetEventTypeId() const { return TEventType::GetStaticEventTypeId(); }
};

// ============================================
// 预定义游戏常用事件（可按需扩展）
// ============================================

// 玩家生命周期
struct FPlayerLoginEvent   : MEvent<FPlayerLoginEvent>   { uint64 PlayerId = 0; uint32 Level = 0; };
struct FPlayerLogoutEvent  : MEvent<FPlayerLogoutEvent>  { uint64 PlayerId = 0; };
struct FPlayerEnterSceneEvent : MEvent<FPlayerEnterSceneEvent> { uint64 PlayerId = 0; uint32 SceneId = 0; };
struct FPlayerLeaveSceneEvent : MEvent<FPlayerLeaveSceneEvent> { uint64 PlayerId = 0; uint32 SceneId = 0; };

// 战斗事件
struct FCombatStartEvent    : MEvent<FCombatStartEvent>    { uint64 PlayerId = 0; uint32 SceneId = 0; };
struct FCombatEndEvent      : MEvent<FCombatEndEvent>      { uint64 PlayerId = 0; bool bVictory = false; uint32 DamageDealt = 0; };
struct FSkillCastEvent      : MEvent<FSkillCastEvent>      { uint64 PlayerId = 0; uint32 SkillId = 0; bool bSuccess = false; };

// 社交事件
struct FTradeRequestEvent    : MEvent<FTradeRequestEvent>   { uint64 FromPlayerId = 0; uint64 ToPlayerId = 0; uint64 TradeSessionId = 0; };
struct FTradeCompleteEvent  : MEvent<FTradeCompleteEvent>  { uint64 TradeSessionId = 0; bool bSuccess = false; };

// 物品/经济事件
struct FItemAcquiredEvent   : MEvent<FItemAcquiredEvent>   { uint64 PlayerId = 0; uint32 ItemId = 0; uint32 Count = 0; };
struct FGoldChangedEvent    : MEvent<FGoldChangedEvent>    { uint64 PlayerId = 0; int32 Delta = 0; uint32 NewValue = 0; };
struct FLevelUpEvent        : MEvent<FLevelUpEvent>        { uint64 PlayerId = 0; uint32 OldLevel = 0; uint32 NewLevel = 0; };

// 连接事件（用于监督系统）
struct FConnectionLostEvent  : MEvent<FConnectionLostEvent> { uint64 PlayerId = 0; uint64 ConnectionId = 0; };
