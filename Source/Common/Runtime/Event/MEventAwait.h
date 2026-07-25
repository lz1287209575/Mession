#pragma once

#include "Common/Runtime/Event/MEventBus.h"
#include "Common/Runtime/Concurrency/FiberAwait.h"

/**
 * MEventAwait — 事件可等待（fiber / legacy 路径）
 *
 * 在 fiber 内等待一个事件触发一次后继续执行。
 * 依赖 FiberAwait：必须在 fiber 内使用（player strand 上下文）。
 *
 * 推荐写法（C++17 state-machine 模型）见父 spec:
 *   Docs/superpowers/specs/2026-07-24-cpp17-async-await.md §7.1
 *   (AWAIT / AWAIT_OK) for the cpp17 async/await model.
 *
 * 用法（fiber 内）:
 *   MFunction(Async)
 *   SFutureResult<FPlayerLoginEvent> WaitForLogin(uint64 PlayerId)
 *   {
 *       FPlayerLoginEvent Event;
 *       AWAIT_EVENT(Event);       // fiber 挂起，等待事件触发一次
 *       // spec §7.1 推荐写法：returning SFutureResult via AWAIT_OK + state machine
 *       return SFutureResult<FPlayerLoginEvent>(TResult<FPlayerLoginEvent, FAppError>::Ok(Event));
 *   }
 *
 * 注意：AWAIT_EVENT 宏将事件引用捕获到 lambda 中，
 *       MEventBus::SubscribeOnce 在事件触发时写入引用并 resume fiber。
 *       本头为 legacy fiber 适配层；新代码请走 spec §7 的 cpp17 state-machine 模型。
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
 * MEventBus::Await — 事件可等待工厂方法（fiber / legacy 路径）
 *
 * 用法（fiber 内）：
 *   FPlayerLoginEvent Event;
 *   TEventAwaiter<FPlayerLoginEvent> Awaiter = MEventBus::Await(Event);
 *
 * 见父 spec:
 *   Docs/superpowers/specs/2026-07-24-cpp17-async-await.md §7.1
 *   (AWAIT / AWAIT_OK) for the cpp17 async/await model.
 */
template<typename TEvent>
TEventAwaiter<TEvent> Await(TEvent& OutEvent)
{
    return TEventAwaiter<TEvent>(OutEvent);
}

}  // namespace MEventBus

/**
 * AWAIT_EVENT — 事件可等待简化宏（fiber / legacy 路径）
 *
 * 用法（fiber 内）：
 *   FPlayerLoginEvent Event;
 *   AWAIT_EVENT(Event);  // 挂起直到事件触发
 *
 * 此宏用于 MHeaderTool 生成的续体链中，
 *       MHeaderTool 会将其展开为 fiber-aware 等待逻辑。
 *
 * 新代码请走 spec §7 的 cpp17 state-machine async/await 模型：
 *   Docs/superpowers/specs/2026-07-24-cpp17-async-await.md §7.1
 *   (AWAIT / AWAIT_OK)
 */
#define AWAIT_EVENT(Event) \
    co_await MEventBus::Await(Event)
