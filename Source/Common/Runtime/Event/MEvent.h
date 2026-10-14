/**
 * @file MEvent.h
 * @brief MEvent - 事件基类模板(CRTP)与一组预定义业务事件。
 *
 * @detail 用法:事件结构以自身为模板参数继承 MEvent,例如
 *   struct FPlayerLoginEvent : MEvent<FPlayerLoginEvent> { ... };
 * 从而获得 GetStaticEventTypeId();事件实例则通过 GetEventTypeId() 取同一个 ID,
 * 供 MEventBus 按类型路由。
 * 两个必须知道的事实:
 *   - 基类带 virtual 析构,所以每个事件类型都有 vptr(不是零开销,不能在 ABI 敏感的
 *     地方指望它和裸 struct 同布局);
 *   - ID 由 MEventDetail 里的非模板计数器统一分配,因此不同事件类型的 ID 互不相同;
 *     ID 是首次调用时惰性分配出来的运行期值,不是编译期常量(详见 GetEventTypeIdImpl)。
 * 本文件只定义类型,不做任何运行时注册。
 */
#pragma once

#include "Common/Runtime/MLib.h"

namespace MEventDetail {
    /**
     * @brief AllocateEventTypeId - 分配一个全进程唯一的事件类型 ID。
     * @return 新 ID(从 1 开始递增;0 保留表示无效)
     *
     * @detail 计数器只放在**非模板**函数里是有意为之:静态局部变量在函数模板内是
     * "每个实例化一份",若把它写在 GetEventTypeIdImpl<T>() 体内,每个事件类型都会各自
     * 从 1 开始编号,于是所有类型 ID 都退化成 1(MEventBus 会把它们当成同一个键路由)。
     * 用 std::atomic 是因为首次调用可能来自不同线程,而 ID 一旦分配就不能重复。
     */
    inline uint32 AllocateEventTypeId() {
        static std::atomic<uint32> NextId{1}; // 从 1 开始,0 表示无效
        return NextId.fetch_add(1);
    }

    /**
     * @brief GetEventTypeIdImpl - 取事件类型 T 的类型 ID(每个 T 只算一次)。
     * @return 该模板实例第一次调用时从 AllocateEventTypeId() 取到的 ID(之后恒定)
     *
     * @detail ID 只在本模板实例首次调用时分配一次,结果缓存在该实例自己的静态变量里;
     * 计数器本身在所有实例间共享,因此不会有两个事件类型拿到同一个 ID。
     * 注意:分配顺序取决于首次调用顺序,所以 ID 是运行期惰性分配的值,不是编译期常量。
     */
    template <typename T> uint32 GetEventTypeIdImpl() {
        static const uint32 Id = AllocateEventTypeId();
        return Id;
    }
} // namespace MEventDetail

/**
 * @brief MEvent - 事件基类模板(CRTP,模板参数是事件类型自身)。
 *
 * @detail 使用方式:struct FMyEvent : MEvent<FMyEvent> { ... };
 * TEvent 只用于实例化出唯一的类型 ID,事件本身不需要写任何成员函数。
 * 注意:各事件类型之间没有公共基类(MEvent<A> 与 MEvent<B> 是不同类型),
 * 因此不能用 MEvent* 统一持有事件;跨类型收集需要额外的擦除层。
 */
template <typename TEvent> struct MEvent {
    using TEventType = TEvent;

    /**
     * @brief GetStaticEventTypeId - 取本事件类型的 ID(类型级,不需要实例)。
     * @return 该事件类型的 ID;首次调用时惰性分配(非编译期常量)
     */
    static uint32 GetStaticEventTypeId() {
        return MEventDetail::GetEventTypeIdImpl<TEvent>();
    }

    // 虚析构:让事件对象能安全地按基类指针销毁;代价是每个事件类型多一个 vptr
    virtual ~MEvent() = default;

    /**
     * @brief GetEventTypeId - 取本实例的类型 ID。
     * @return 与 GetStaticEventTypeId() 相同的值(静态派发,不做动态类型识别)
     */
    uint32 GetEventTypeId() const {
        return TEventType::GetStaticEventTypeId();
    }
};

// ============================================
// 预定义游戏常用事件(可按需扩展)
//
// 以下都是纯数据样例,供业务直接使用或作为定义模板;
// 当前仓库内没有它们的发布/订阅点(MEventBus 也暂无调用方)。
// ============================================

// 玩家生命周期
struct FPlayerLoginEvent : MEvent<FPlayerLoginEvent> {
    uint64 PlayerId = 0;
    uint32 Level    = 0;
};
struct FPlayerLogoutEvent : MEvent<FPlayerLogoutEvent> {
    uint64 PlayerId = 0;
};
struct FPlayerEnterSceneEvent : MEvent<FPlayerEnterSceneEvent> {
    uint64 PlayerId = 0;
    uint32 SceneId  = 0;
};
struct FPlayerLeaveSceneEvent : MEvent<FPlayerLeaveSceneEvent> {
    uint64 PlayerId = 0;
    uint32 SceneId  = 0;
};

// 战斗事件
struct FCombatStartEvent : MEvent<FCombatStartEvent> {
    uint64 PlayerId = 0;
    uint32 SceneId  = 0;
};
struct FCombatEndEvent : MEvent<FCombatEndEvent> {
    uint64 PlayerId    = 0;
    bool   bVictory    = false;
    uint32 DamageDealt = 0;
};
struct FSkillCastEvent : MEvent<FSkillCastEvent> {
    uint64 PlayerId = 0;
    uint32 SkillId  = 0;
    bool   bSuccess = false;
};

// 社交事件
struct FTradeRequestEvent : MEvent<FTradeRequestEvent> {
    uint64 FromPlayerId   = 0;
    uint64 ToPlayerId     = 0;
    uint64 TradeSessionId = 0;
};
struct FTradeCompleteEvent : MEvent<FTradeCompleteEvent> {
    uint64 TradeSessionId = 0;
    bool   bSuccess       = false;
};

// 物品/经济事件
struct FItemAcquiredEvent : MEvent<FItemAcquiredEvent> {
    uint64 PlayerId = 0;
    uint32 ItemId   = 0;
    uint32 Count    = 0;
};
struct FGoldChangedEvent : MEvent<FGoldChangedEvent> {
    uint64 PlayerId = 0;
    int32  Delta    = 0;
    uint32 NewValue = 0;
};
struct FLevelUpEvent : MEvent<FLevelUpEvent> {
    uint64 PlayerId = 0;
    uint32 OldLevel = 0;
    uint32 NewLevel = 0;
};

// 连接事件(用于监督系统)
struct FConnectionLostEvent : MEvent<FConnectionLostEvent> {
    uint64 PlayerId     = 0;
    uint64 ConnectionId = 0;
};
