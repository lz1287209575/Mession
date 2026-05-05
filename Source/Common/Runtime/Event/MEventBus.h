#pragma once

#include "Common/Runtime/Event/MEvent.h"
#include "Common/Runtime/Event/MDelegate.h"

/**
 * MEventBus - 全局事件总线（type-erased 实现）
 *
 * 核心设计：
 * - Handler 存储为 TFunction<void(const void*)>，完全类型擦除
 * - 无需预声明事件类型，任意 MEvent 子类即可发布/订阅
 * - 每个事件类型独立列表，发布时 O(1) 定位
 * - 一次性订阅通过内部标记实现
 * - 线程安全由调用方保证（通常在 strand 内 Publish）
 *
 * 用法示例：
 *
 *   // 1. 定义事件
 *   struct FPlayerLoginEvent : MEvent<FPlayerLoginEvent> {
 *       uint64 PlayerId = 0;
 *       uint32 Level = 0;
 *   };
 *
 *   // 2. 在系统初始化时订阅
 *   MEventBus::Subscribe<FPlayerLoginEvent>(
 *       this, &AchievementSystem::OnPlayerLogin);
 *
 *   // 3. 事件发生时发布
 *   FPlayerLoginEvent e{ .PlayerId = 1, .Level = 10 };
 *   MEventBus::Publish(&e);
 *
 *   // 4. 一次性订阅
 *   MEventSubscription Sub = MEventBus::Subscribe<FCombatEndEvent>(
 *       this, &LootSystem::OnCombatEnd);
 *   Sub.OneShot();  // 触发一次后自动退订
 *
 * 注意：MEventBus 是单例，所有服务器共享。
 */

class MEventBus
{
public:
    static MEventBus& Get()
    {
        static MEventBus Instance;
        return Instance;
    }

    // ================================
    // 订阅（模板版本，类型安全）
    // ================================

    // 普通订阅，返回句柄
    template<typename TEvent>
    MEventSubscription Subscribe(
        void* Object,
        void (*Handler)(void*, const TEvent*))
    {
        uint32 TypeId = TEvent::GetStaticEventTypeId();
        auto& List = GetOrCreateList(TypeId);
        uint32 Handle = static_cast<uint32>(List.size());
        List.push_back(MakeErasedHandler<TEvent>(Object, Handler));
        return { Handle, TypeId };
    }

    // 普通订阅（成员函数版本，最常用）
    template<typename TObject, typename TEvent>
    MEventSubscription Subscribe(
        TObject* Object,
        void (TObject::*Method)(const TEvent*))
    {
        return Subscribe<TEvent>(Object, [](void* Obj, const TEvent* E) {
            (static_cast<TObject*>(Obj)->*Method)(E);
        });
    }

    // 一次性订阅（触发一次后自动退订）
    template<typename TEvent>
    MEventSubscription SubscribeOnce(
        void* Object,
        void (*Handler)(void*, const TEvent*))
    {
        MEventSubscription Sub = Subscribe<TEvent>(Object, Handler);
        Sub.OneShot();
        uint32 TypeId = TEvent::GetStaticEventTypeId();
        OneShotHandles[TypeId].insert(Sub.Handle);
        return Sub;
    }

    template<typename TObject, typename TEvent>
    MEventSubscription SubscribeOnce(
        TObject* Object,
        void (TObject::*Method)(const TEvent*))
    {
        MEventSubscription Sub = Subscribe<TEvent>(Object, Method);
        Sub.OneShot();
        uint32 TypeId = TEvent::GetStaticEventTypeId();
        OneShotHandles[TypeId].insert(Sub.Handle);
        return Sub;
    }

    // ================================
    // 退订
    // ================================

    // 通过句柄退订
    void Unsubscribe(const MEventSubscription& Sub)
    {
        if (!Sub.IsValid())
        {
            return;
        }

        auto It = HandlerMap.find(Sub.EventTypeId);
        if (It == HandlerMap.end())
        {
            return;
        }

        auto& List = It->second;
        if (Sub.Handle < List.size())
        {
            List[Sub.Handle] = nullptr;  // 标记为空，后续广播跳过
        }
    }

    // 清空某个事件类型的所有订阅
    void UnsubscribeAll(uint32 EventTypeId)
    {
        auto It = HandlerMap.find(EventTypeId);
        if (It != HandlerMap.end())
        {
            auto& List = It->second;
            List.clear();
        }
        OneShotHandles.erase(EventTypeId);
    }

    // 清空所有事件的所有订阅（服务器关闭时）
    void ClearAll()
    {
        for (auto& Pair : HandlerMap)
        {
            Pair.second.clear();
        }
        HandlerMap.clear();
        OneShotHandles.clear();
    }

    // ================================
    // 发布
    // ================================

    template<typename TEvent>
    void Publish(const TEvent* Event)
    {
        uint32 TypeId = TEvent::GetStaticEventTypeId();
        auto It = HandlerMap.find(TypeId);
        if (It == HandlerMap.end())
        {
            return;
        }

        auto& List = It->second;
        bool HasOneShot = false;

        for (size_t i = 0; i < List.size(); ++i)
        {
            auto& Handler = List[i];
            if (!Handler)
            {
                continue;
            }

            // 调用 handler，Event 指针被 const void* 接收
            const void* EventPtr = static_cast<const void*>(Event);
            Handler(EventPtr);

            // 检查是否是一次性订阅
            auto ShotIt = OneShotHandles.find(TypeId);
            if (ShotIt != OneShotHandles.end() && ShotIt->second.count(static_cast<uint32>(i)))
            {
                Handler = nullptr;  // 标记移除
                HasOneShot = true;
            }
        }

        // 清理一次性订阅句柄
        if (HasOneShot)
        {
            OneShotHandles.erase(TypeId);
        }
    }

    template<typename TEvent>
    void Publish(const TEvent& Event)
    {
        Publish<TEvent>(&Event);
    }

    // ================================
    // 诊断
    // ================================

    template<typename TEvent>
    size_t GetSubscriberCount() const
    {
        uint32 TypeId = TEvent::GetStaticEventTypeId();
        return GetSubscriberCountById(TypeId);
    }

    size_t GetSubscriberCountById(uint32 EventTypeId) const
    {
        auto It = HandlerMap.find(EventTypeId);
        if (It == HandlerMap.end())
        {
            return 0;
        }
        const auto& List = It->second;
        size_t Count = 0;
        for (const auto& H : List)
        {
            if (H) ++Count;
        }
        return Count;
    }

    // 获取已注册的事件类型数量
    size_t GetEventTypeCount() const { return HandlerMap.size(); }

private:
    // type-erased handler: TFunction<void(const void*)>

    MEventBus() = default;

    TVector<TFunction<void(const void*)>>& GetOrCreateList(uint32 TypeId)
    {
        return HandlerMap[TypeId];
    }

    // 将成员函数 handler 打包成 type-erased function
    template<typename TEvent>
    static TFunction<void(const void*)> MakeErasedHandler(
        void* Object,
        void (*Handler)(void*, const TEvent*))
    {
        return [Object, Handler](const void* Event) {
            Handler(Object, static_cast<const TEvent*>(Event));
        };
    }

    // 事件类型 ID -> 处理器列表（type-erased）
    // 每个 TEvent 的 GetStaticEventTypeId() 作为 key
    TMap<uint32, TVector<TFunction<void(const void*)>>> HandlerMap;

    // 一次性订阅的句柄集合
    // EventTypeId -> 句柄集合
    TMap<uint32, TSet<uint32>> OneShotHandles;

    MEventBus(const MEventBus&) = delete;
    MEventBus& operator=(const MEventBus&) = delete;
};
