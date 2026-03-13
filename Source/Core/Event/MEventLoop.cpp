#include "MEventLoop.h"

void MEventLoop::AddStep(IEventLoopStep* step, int timeoutMs)
{
    if (step != nullptr)
    {
        Steps.push_back({ step, timeoutMs });
    }
}

void MEventLoop::RunOnce()
{
    for (SStep& S : Steps)
    {
        if (S.Step != nullptr)
        {
            S.Step->RunOnce(S.TimeoutMs);
        }
    }
}

void MEventLoop::Run()
{
    bRunning = true;
    while (bRunning)
    {
        RunOnce();
    }
}
