#pragma once

#include "Common/Runtime/Event/MEventBus.h"
#include "Common/Runtime/Concurrency/FiberAwait.h"

/**
 * MEventAwait — 事件可等待
 *
 * 在 async 函数中等待一个事件触发一次后继续执行。
 * 依赖 FiberAwait：必须在 fiber 内使用（player strand 上下文）。
 *
 * 用法：
 *
 *   // 方式1：MEventBus::Await<T>(event) 工厂方法
 *   MFunction(Async)
 *   MFUTURE(FPlayerLoginEvent) WaitForLogin(uint64 PlayerId)
 *   {
 *       FPlayerLoginEvent Event;
 *       AWAIT_EVENT(Event);       // fiber 挂起，等待事件触发一次
 *       co_return Event;
 *   }
 *
 *   // 方式2：直接用 co_await（需要 C++20 coroutine，MHeaderTool 生成续体时特殊处理）
 *   FPlayerLoginEvent Event;
 *   co_await MEventBus::Await(Event);
 *   co_return Event;
 *
 * 注意：AWAIT_EVENT 宏将事件引用捕获到 lambda 中，
 *       MEventBus::SubscribeOnce 在事件触发时写入引用并 resume fiber。
 */

namespace MEventBus
{

template<typename TEvent>
class TEventAwaiter
{
public:
    explicit TEventAwaiter(TEvent& OutEvent)
        : OutEvent(&OutEvent)
    {}

    // 不在 fiber 内时，同步返回（不挂起）
    bool await_ready() const
    {
        return !MHasCurrentPlayerCommand();
    }

    // 挂起 fiber，订阅一次性事件，触发时 resume
    void await_suspend(TFunction<void()> Resume)
    {
        MEventBus::SubscribeOnce<TEvent>(
            nullptr,
            [this, Resume](const TEvent* E) mutable {
                *OutEvent = *E;
                Subscription = MEventSubscription{};  // 已有 OneShot，无需显式退订
                Resume();
            });
    }

    // 返回事件引用
    TEvent& await_resume() const
    {
        return *OutEvent;
    }

private:
    TEvent* OutEvent = nullptr;
    MEventSubscription Subscription;
};

/**
 * MEventBus::Await — 事件可等待工厂方法
 *
 * 用法：
 *   FPlayerLoginEvent Event;
 *   co_await MEventBus::Await(Event);
 */
template<typename TEvent>
TEventAwaiter<TEvent> Await(TEvent& OutEvent)
{
    return TEventAwaiter<TEvent>(OutEvent);
}

}  // namespace MEventBus

/**
 * AWAIT_EVENT — 事件可等待简化宏
 *
 * 用法：
 *   FPlayerLoginEvent Event;
 *   AWAIT_EVENT(Event);  // 挂起直到事件触发
 *
 * 等价于：
 *   co_await MEventBus::Await(Event);
 *
 * 注意：此宏用于 MHeaderTool 生成的续体链中，
 *       MHeaderTool 会将其展开为 fiber-aware 等待逻辑。
 */
#define AWAIT_EVENT(Event) \
    co_await MEventBus::Await(Event)
