/**
 * @file MActorHandle.h
 * @brief MActorHandle - actor 代理,从任意线程 Post/Call 业务 actor.
 */
#pragma once

#include "Common/Runtime/Actor/FActorMessage.h"
#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/MLib.h"

class MRpcChannel;
class MSubReactorPool;
class IActor;

/**
 * @brief MActorHandle - actor 句柄(任意线程可用).
 *
 * 拿到 Handle 后,从任意线程调 Post/Call:
 * - Post:异步,fire-and-forget
 * - Call:同步,返回 SFutureResult<TByteArray>
 *
 * 实际执行永远在 actor 自己的 Sub 线程(单线程访问 actor state)。
 *
 * 所有权:Handle 持 TSharedPtr<IActor> 副本 —— Unregister 后旧 Handle
 * 仍能安全使用(shared_ptr 保活直到消息处理完),不会悬空。
 */
class MActorHandle {
    public:
    MActorHandle() = default;

    /**
     * @brief 构造本地 actor 句柄.
     * @param InActor   actor 共享指针(Handle 持副本)
     * @param InSubPool 用于拿 Sub 的 ambient
     */
    MActorHandle(TSharedPtr<IActor> InActor, MSubReactorPool* InSubPool);

    /**
     * @brief 构造远端 actor 句柄(占位,Post/Call 走 MRpcChannel::CallToActor).
     */
    explicit MActorHandle(uint64 InActorId);

    /** @brief 异步发消息(无返回值). */
    void Post(const FActorMessage& InMsg) const;

    /** @brief 同步调用(返回 future,Call 完成后由 actor 自己 SetValue). */
    SFutureResult<TByteArray> Call(FActorMessage InMsg) const;

    /** @return actor id. */
    uint64 GetActorId() const {
        return ActorId;
    }

    /** @return 是否本进程内的 actor. */
    bool IsLocal() const {
        return Actor != nullptr;
    }

    /** @return 本进程内的 actor(远端句柄返回空). */
    IActor* GetActor() const {
        return Actor.Get();
    }

    private:
    uint64             ActorId = 0;
    TSharedPtr<IActor> Actor;             // 本进程时有效
    MSubReactorPool*   SubPool = nullptr; // 本进程时有效
};