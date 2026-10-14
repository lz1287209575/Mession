/**
 * @file MEventBus.h
 * @brief MEventBus - 进程级事件总线(type-erased 实现)。
 *
 * @detail 核心设计:
 *   - 处理函数被擦除成 TFunction<void(const void*)>,存进以事件类型 ID 为键的 TMap;
 *   - 发布/订阅不需要预声明事件类型,任意 MEvent 子类都能用;
 *   - 订阅列表是平铺 TVector,退订靠"置空槽位",句柄因此长期稳定;
 *   - 一次性订阅靠 OneShotHandles 侧表实现(见 SubscribeOnce / Publish);
 *   - 保存的是对象裸指针,不管理被订阅者生命周期,也不校验其有效性。
 * 线程安全:完全没有内部加锁,由调用方保证(通常在 strand / 单线程 loop 内 Publish)。
 *
 * 用法示例:
 *
 *   // 1. 定义事件(继承链末尾是 MEvent 基类)
 *   struct FPlayerLoginEvent : MEvent<FPlayerLoginEvent> {
 *       uint64 PlayerId;
 *       uint32 Level;
 *   };
 *
 *   // 2. 在系统初始化时订阅
 *   MEventBus::Get().Subscribe(this, &AchievementSystem::OnPlayerLogin);
 *
 *   // 3. 事件发生时发布
 *   FPlayerLoginEvent e{ .PlayerId = 1, .Level = 10 };
 *   MEventBus::Get().Publish(e);
 *
 *   // 4. 一次性订阅(触发一次后自动退订)
 *   MEventSubscription Sub = MEventBus::Get().SubscribeOnce(this, &LootSystem::OnCombatEnd);
 *
 * 注意:MEventBus 是进程级单例(Get()),所有模块共享同一份订阅表;构造被设为私有,
 * 因此无法按模块创建独立总线。
 */
#pragma once

#include "Common/Runtime/Event/MDelegate.h"
#include "Common/Runtime/Event/MEvent.h"

/**
 * @brief MEventBus - 单例事件总线。
 */
class MEventBus {
    public:
    /**
     * @brief Get - 进程级单例(首次调用时构造)。
     */
    static MEventBus& Get() {
        static MEventBus Instance;
        return Instance;
    }

    // ================================
    // 订阅（模板版本，类型安全）
    // ================================

    /**
     * @brief Subscribe - 订阅事件(自由函数形式)。
     * @param Object  订阅者对象(裸指针,不拥有;须在退订前保持有效)
     * @param Handler 形如 void(void* Object, const TEvent*) 的处理函数
     * @return 订阅句柄(Handle = 槽位下标 + 1,EventTypeId = 事件类型 ID)
     *
     * @detail 句柄永不复用:退订只置空槽位,再次订阅会在末尾追加新槽位。
     * 重复订阅同一个 (对象, 函数) 不会去重,会被调用多次。
     * 句柄从 1 开始编号(0 保留给"无效句柄"),因此**第一个订阅者的句柄是 1 而不是 0**,
     * 可以与 MEventSubscription::IsValid() 的 Handle != 0 判定共存。
     */
    template <typename TEvent> MEventSubscription Subscribe(void* Object, void (*Handler)(void*, const TEvent*)) {
        uint32 TypeId = TEvent::GetStaticEventTypeId();
        auto&  List   = GetOrCreateList(TypeId);
        uint32 Handle = static_cast<uint32>(List.size()) + 1; // 1 起编号:0 保留给无效句柄
        List.push_back(MakeErasedHandler<TEvent>(Object, Handler));
        return {Handle, TypeId};
    }

    /**
     * @brief Subscribe - 订阅事件(成员函数形式,最常用)。
     * @param Object 订阅者对象(非拥有)
     * @param Method 成员函数指针
     * @return 订阅句柄
     *
     * @detail 内部把成员函数指针包成无捕获 lambda,再走自由函数版本;
     * lambda 里只做 static_cast,不做虚函数派发。
     */
    template <typename TObject, typename TEvent> MEventSubscription Subscribe(TObject* Object, void (TObject::*Method)(const TEvent*)) {
        return Subscribe<TEvent>(Object, [](void* Obj, const TEvent* E) { (static_cast<TObject*>(Obj)->*Method)(E); });
    }

    /**
     * @brief SubscribeOnce - 订阅事件,且发布一次后自动退订。
     * @return 订阅句柄(已同时写入 OneShotHandles 侧表)
     *
     * @detail 与"Subscribe 之后手工调 Sub.OneShot()"的区别:这个版本会真正登记到
     * OneShotHandles 里,因此 Publish 会替它退订;手工 OneShot() 只改标记位,不会自动退订。
     */
    template <typename TEvent> MEventSubscription SubscribeOnce(void* Object, void (*Handler)(void*, const TEvent*)) {
        MEventSubscription Sub = Subscribe<TEvent>(Object, Handler);
        Sub.OneShot();
        uint32 TypeId = TEvent::GetStaticEventTypeId();
        OneShotHandles[TypeId].insert(Sub.Handle);
        return Sub;
    }

    /**
     * @brief SubscribeOnce - 订阅事件(成员函数形式),发布一次后自动退订。
     */
    template <typename TObject, typename TEvent> MEventSubscription SubscribeOnce(TObject* Object, void (TObject::*Method)(const TEvent*)) {
        MEventSubscription Sub = Subscribe<TEvent>(Object, Method);
        Sub.OneShot();
        uint32 TypeId = TEvent::GetStaticEventTypeId();
        OneShotHandles[TypeId].insert(Sub.Handle);
        return Sub;
    }

    // ================================
    // 退订
    // ================================

    /**
     * @brief Unsubscribe - 按句柄退订。
     * @param Sub Subscribe / SubscribeOnce 返回的句柄
     *
     * @detail 只把对应槽位置空(不压缩、不复用),因此其它句柄继续有效;
     * 同时**不**清理 OneShotHandles —— 一次性订阅退订后会留下一个永不触发的句柄记录。
     * 句柄是"槽位下标 + 1"(见 Subscribe),所以默认构造 / Reset() 之后的 Handle == 0 会被
     * IsValid() 判为无效而直接返回;真实的第一个订阅者(句柄 1)可以正常退订。
     */
    void Unsubscribe(const MEventSubscription& Sub) {
        if (!Sub.IsValid()) {
            return;
        }

        auto It = HandlerMap.find(Sub.EventTypeId);
        if (It == HandlerMap.end()) {
            return;
        }

        auto&        List  = It->second;
        const uint32 Index = Sub.Handle - 1; // 句柄 = 槽位下标 + 1
        if (Index < List.size()) {
            List[Index] = nullptr; // 标记为空，后续广播跳过
        }
    }

    /**
     * @brief UnsubscribeAll - 清空某个事件类型的全部订阅。
     * @param EventTypeId 事件类型 ID(通常取自 TEvent::GetStaticEventTypeId())
     *
     * @detail 该类型下所有已发出的句柄立即失效;同时清掉该类型的一次性订阅记录。
     */
    void UnsubscribeAll(uint32 EventTypeId) {
        auto It = HandlerMap.find(EventTypeId);
        if (It != HandlerMap.end()) {
            auto& List = It->second;
            List.clear();
        }
        OneShotHandles.erase(EventTypeId);
    }

    /**
     * @brief ClearAll - 清空所有事件的所有订阅(进程关闭时使用)。
     */
    void ClearAll() {
        for (auto& Pair : HandlerMap) {
            Pair.second.clear();
        }
        HandlerMap.clear();
        OneShotHandles.clear();
    }

    // ================================
    // 发布
    // ================================

    /**
     * @brief Publish - 同步发布事件(逐个调用本类型的处理函数)。
     * @param Event 事件对象;以裸指针转交,调用期间必须保持有效
     *
     * @detail 执行模型:先按 ID 在 HandlerMap 里定位列表(未订阅过则直接返回),
     * 再按槽位顺序同步调用 —— 处理函数若在回调中 Subscribe/Unsubscribe,会实时影响
     * 本次遍历(新增的槽位可能被本轮就调用到,而被置空的槽位被跳过)。
     * 没有异常保护:某个处理函数抛异常会中断后续处理函数。
     * 一次性订阅的处理方式:检查该槽位是否在 OneShotHandles 里,是则置空该槽位,并且只从
     * 侧表里摘掉**这一个**句柄 —— 同类型下本轮没轮到触发的一次性订阅仍保留下次自动退订的
     * 能力;只有该类型的侧表条目空了才删除整个 key。
     */
    template <typename TEvent> void Publish(const TEvent* Event) {
        uint32 TypeId = TEvent::GetStaticEventTypeId();
        auto   It     = HandlerMap.find(TypeId);
        if (It == HandlerMap.end()) {
            return;
        }

        auto& List = It->second;

        for (size_t i = 0; i < List.size(); ++i) {
            auto& Handler = List[i];
            if (!Handler) {
                continue;
            }

            // 调用 handler:事件指针被擦除成 const void* 传入
            const void* EventPtr = static_cast<const void*>(Event);
            Handler(EventPtr);

            // 检查是否是一次性订阅:只移除本次真正触发的这一个句柄
            auto ShotIt = OneShotHandles.find(TypeId);
            if (ShotIt != OneShotHandles.end()) {
                const uint32 Handle   = static_cast<uint32>(i) + 1; // 句柄 = 槽位下标 + 1
                auto         HandleIt = ShotIt->second.find(Handle);
                if (HandleIt != ShotIt->second.end()) {
                    Handler = nullptr; // 标记移除
                    ShotIt->second.erase(HandleIt);
                    if (ShotIt->second.empty()) {
                        OneShotHandles.erase(ShotIt); // 该类型已无待触发的一次性订阅
                    }
                }
            }
        }
    }

    /**
     * @brief Publish - 引用形式的便捷重载。
     */
    template <typename TEvent> void Publish(const TEvent& Event) {
        Publish<TEvent>(&Event);
    }

    // ================================
    // 诊断
    // ================================

    /**
     * @brief GetSubscriberCount - 某事件类型当前的**有效**订阅数(已置空的槽位不计)。
     */
    template <typename TEvent> size_t GetSubscriberCount() const {
        uint32 TypeId = TEvent::GetStaticEventTypeId();
        return GetSubscriberCountById(TypeId);
    }

    /**
     * @brief GetSubscriberCountById - 按事件类型 ID 统计有效订阅数。
     */
    size_t GetSubscriberCountById(uint32 EventTypeId) const {
        auto It = HandlerMap.find(EventTypeId);
        if (It == HandlerMap.end()) {
            return 0;
        }
        const auto& List  = It->second;
        size_t      Count = 0;
        for (const auto& H : List) {
            if (H)
                ++Count;
        }
        return Count;
    }

    /**
     * @brief GetEventTypeCount - 已出现过订阅的事件类型数量(= HandlerMap 条目数)。
     */
    size_t GetEventTypeCount() const {
        return HandlerMap.size();
    }

    private:
    // 处理函数统一擦除成 TFunction<void(const void*)>

    MEventBus() = default;

    /**
     * @brief GetOrCreateList - 取(必要时创建)某事件类型的处理函数列表。
     * @param TypeId 事件类型 ID
     * @return 列表引用;不存在时会插入一个空列表(因此会改变 GetEventTypeCount())
     *
     * @detail 只被 Subscribe 调用:负责"首次订阅时建表"。
     */
    TVector<TFunction<void(const void*)>>& GetOrCreateList(uint32 TypeId) {
        return HandlerMap[TypeId];
    }

    /**
     * @brief MakeErasedHandler - 把 (对象, 自由函数) 打包成类型擦除的处理函数。
     * @param Object  订阅者裸指针(被 lambda 按值捕获,不拥有)
     * @param Handler 处理函数指针
     * @return 接受 const void* 的 TFunction;调用时把事件指针还原成 const TEvent*
     */
    template <typename TEvent> static TFunction<void(const void*)> MakeErasedHandler(void* Object, void (*Handler)(void*, const TEvent*)) {
        return [Object, Handler](const void* Event) { Handler(Object, static_cast<const TEvent*>(Event)); };
    }

    // 事件类型 ID -> 处理函数列表(type-erased)
    // key 取自 TEvent::GetStaticEventTypeId()
    TMap<uint32, TVector<TFunction<void(const void*)>>> HandlerMap;

    // 一次性订阅的句柄集合
    // EventTypeId -> 句柄集合
    TMap<uint32, TSet<uint32>> OneShotHandles;

    MEventBus(const MEventBus&)            = delete;
    MEventBus& operator=(const MEventBus&) = delete;
};
