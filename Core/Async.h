#pragma once

#include "NetCore.h"
#include "EventLoop.h"

namespace MAsync
{

/** 将 Next 投递到 Loop 的下一轮 RunOnce 执行（即“让出”到下一 tick）。 */
inline void Yield(MNetEventLoop* Loop, TFunction<void()> Next)
{
    if (Loop && Next)
    {
        Loop->PostTask(std::move(Next));
    }
}

/** 多步序列：Do(step1); Do(step2); Run()；步与步之间在下一 tick 自动衔接。 */
class MSequence : public TEnableSharedFromThis<MSequence>
{
public:
    explicit MSequence(MNetEventLoop* InLoop) : Loop(InLoop) {}

    static TSharedPtr<MSequence> Create(MNetEventLoop* Loop)
    {
        return MakeShared<MSequence>(Loop);
    }

    void Do(TFunction<void()> Step)
    {
        if (Step)
        {
            Steps.push_back(std::move(Step));
        }
    }

    void Run()
    {
        if (Steps.empty())
        {
            return;
        }
        RunStep(0);
    }

private:
    void RunStep(size_t i)
    {
        if (i >= Steps.size())
        {
            return;
        }
        Steps[i]();
        if (i + 1 < Steps.size())
        {
            TSharedPtr<MSequence> Self = shared_from_this();
            const size_t Next = i + 1;
            Loop->PostTask([Self, Next]()
            {
                Self->RunStep(Next);
            });
        }
    }

    MNetEventLoop* Loop = nullptr;
    TVector<TFunction<void()>> Steps;
};

}
