#include "Common/Runtime/Actor/MActorHandle.h"

#include "Common/Net/Rpc/MRpcChannel.h"
#include "Common/Runtime/Actor/FActorMessage.h"
#include "Common/Runtime/Actor/IActor.h"
#include "Common/Runtime/Actor/MActorSystem.h"
#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/SubReactorPool.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Object/Result.h"

#include <cstdlib>

MActorHandle::MActorHandle(TSharedPtr<IActor> InActor, MSubReactorPool* InSubPool) : ActorId(InActor != nullptr ? InActor->GetActorId() : 0), Actor(std::move(InActor)), SubPool(InSubPool) {
}

MActorHandle::MActorHandle(uint64 InActorId) : ActorId(InActorId), Actor(nullptr), SubPool(nullptr) {
}

void MActorHandle::Post(const FActorMessage& InMsg) const {
    if (!IsLocal()) {
        // 远端 actor —— 走 MActorSystem::SendActor(真 fire-and-forget):
        // - 不分配 MPromise
        // - 不 RegisterServerCall
        // - server OnActorMessage 收到 → DispatchLocal → 不回任何包（0 RTT wasted）
        // - 失败 → 自动入 outbox,带 SequenceId 等 ack
        MActorSystem::SendActor(ActorId, InMsg);
        return;
    }

    if (SubPool == nullptr) {
        LOG_FATAL("MActorHandle::Post with null SubPool");
        std::abort();
    }

    // 选 actor 自己的 Sub(MActorSystem::Register 时记录的 SubId)
    // 简化:用 hash(ActorId) % SubCount 选 Sub
    // 真生产:从 MActorSystem 拿 SubId 索引
    const uint32 SubCount = SubPool->GetSubCount();
    if (SubCount == 0) {
        LOG_FATAL("MActorHandle::Post with 0 subs");
        std::abort();
    }
    const uint32 SubId = static_cast<uint32>(ActorId % SubCount);

    MAsync::MAsyncContext* Ambient = SubPool->GetAmbient(SubId);
    if (Ambient == nullptr) {
        LOG_FATAL("MActorHandle::Post with null Ambient");
        std::abort();
    }

    // 拷贝消息避免 caller 释放
    FActorMessage Captured = InMsg;
    Ambient->Post([Actor = this->Actor, Captured]() mutable {
        // 这里已经在 actor 自己的 Sub 线程
        Actor->OnMessage(Captured);
    });
}

SFutureResult<TByteArray> MActorHandle::Call(FActorMessage InMsg) const {
    // Call 同步调用 — Promise 持 TResult(TByteArray, FAppError)
    // 因为 SFutureResult<TByteArray> 内部是 MFuture<TResult<TByteArray, FAppError>>
    TSharedPtr<MPromise<TResult<TByteArray, FAppError>>> Promise = MakeShared<MPromise<TResult<TByteArray, FAppError>>>();
    SFutureResult<TByteArray>                            Future(Promise->GetFuture());
    InMsg.ReplyPromise = Promise;

    if (!IsLocal()) {
        // 远端 Call —— 不靠 Msg.ReplyPromise 跨进程(共享指针不上 wire),
        // 走 MRpcChannel::CallActorAndWait:对端 OnActorCall 等 actor 处理完后
        // 通过 ServerCall response 通道回字节给本进程,这里 bridge 到 Promise。
        MRpcChannel::Get()
            .CallActorAndWait(ActorId, InMsg)
            .Then([Promise](MFuture<TResult<TByteArray, FAppError>> F) mutable {
                TResult<TByteArray, FAppError> R = F.Get();
                if (R.IsErr()) {
                    Promise->SetValue(TResult<TByteArray, FAppError>::Err(R.GetError()));
                    return;
                }
                Promise->SetValue(TResult<TByteArray, FAppError>::Ok(R.GetValue()));
            });
        return Future;
    }

    // 本进程 Call —— Post 把 ReplyPromise 串到 actor 端,actor 处理时直接 SetValue resolve Promise。
    Post(InMsg);
    return Future;
}