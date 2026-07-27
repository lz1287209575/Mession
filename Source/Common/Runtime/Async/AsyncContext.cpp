#include "Common/Runtime/Async/AsyncContext.h"

#include <cstdio>

namespace MAsync
{

namespace
{
    thread_local MAsyncContext* GCurrentContext = nullptr;
}

MAsyncContext* MAsyncContext::Current()
{
    return GCurrentContext;
}

void MAsyncContext::SetCurrent(MAsyncContext* Ctx)
{
    GCurrentContext = Ctx;
}

MLoopAsyncContext::MLoopAsyncContext(ITaskRunner* InRunner)
    : Runner(InRunner)
{
}

void MLoopAsyncContext::Post(TFunction<void()> Continuation)
{
    if (Runner && Continuation)
    {
        Runner->PostTask(std::move(Continuation));
    }
    else if (Continuation)
    {
        std::fprintf(stderr, "MLoopAsyncContext::Post: null Runner; continuation dropped\n");
    }
}

bool MLoopAsyncContext::IsSameContext() const
{
    return Runner ? Runner->IsCurrentThread() : false;
}

} // namespace MAsync
