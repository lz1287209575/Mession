#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Concurrency/ITaskRunner.h"

// =========================================================================
// MAsyncContext — 异步上下文（spec §9 薄 SynchronizationContext）
//
// 一个 MAsyncContext 代表一条「执行线」（process event loop / future
// actor strand 等）。AWAIT / Then 的恢复目标由 ambient Current() 决定。
// 与 .NET SynchronizationContext 相比只保留最小子集：
//   * Post(Continuation) — 将续体投递到此 context 的执行线
//   * IsSameContext()   — Get 红线检测；true 表示当前线程就是 context 的执行线
//   * Current()/SetCurrent() — TLS ambient
//
// v1 默认实现 MLoopAsyncContext 绑 ITaskRunner（process event loop）。
// =========================================================================

namespace MAsync
{

class MAsyncContext
{
public:
    virtual ~MAsyncContext() = default;

    /** 将续体投递到此 context 的执行线。May be called from any thread. */
    virtual void Post(TFunction<void()> Continuation) = 0;

    /** True iff the calling thread is this context's loop. Used by Get-redline. */
    virtual bool IsSameContext() const { return false; }

    /** Thread-local ambient pointer. May return nullptr (no ambient). */
    static MAsyncContext* Current();

    /** TLS ambient setter (used by per-process install; never call from business code). */
    static void SetCurrent(MAsyncContext* Ctx);
};

/** 默认 context:绑到进程的 ITaskRunner（通常 = MNetServerBase::TaskLoop）。 */
class MLoopAsyncContext final : public MAsyncContext
{
public:
    explicit MLoopAsyncContext(ITaskRunner* InRunner);

    void Post(TFunction<void()> Continuation) override;
    bool IsSameContext() const override;   // returns Runner->IsCurrentThread()

private:
    ITaskRunner* Runner = nullptr;  // non-owning; lifetime managed by MNetServerBase
};

} // namespace MAsync
